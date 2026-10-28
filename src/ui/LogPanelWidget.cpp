#include "LogPanelWidget.h"
#include "../mqtt/PacketDump.h"

#include <QPlainTextEdit>
#include <QCheckBox>
#include <QSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFont>
#include <QFontMetrics>
#include <QFileDialog>
#include <QTextStream>
#include <QMessageBox>
#include <QScrollBar>
#include <QStringList>
#include <QTimer>
#include <QDateTime>
#include <QTime>

LogPanelWidget::LogPanelWidget(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
}

void LogPanelWidget::buildUi()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    QHBoxLayout *toolBar = new QHBoxLayout;
    m_chkIn = new QCheckBox(QStringLiteral("记录上行 IN"), this);
    m_chkIn->setChecked(true);    // BUGFIX: 默认必须勾选，否则看不到小车上报的消息
    m_chkIn->setToolTip(QStringLiteral("打印小车上报到服务端的消息"));
    m_chkOut = new QCheckBox(QStringLiteral("记录下行 OUT"), this);
    m_chkOut->setToolTip(QStringLiteral("打印服务端下发给小车的消息（含指令）"));
    m_chkAutoScroll = new QCheckBox(QStringLiteral("自动滚动"), this);
    m_chkAutoScroll->setChecked(true);
    m_chkLogFile = new QCheckBox(QStringLiteral("落盘到文件"), this);
    m_chkLogFile->setToolTip(QStringLiteral("将日志写入文件"));
    m_chkFullPayload = new QCheckBox(QStringLiteral("完整载荷"), this);
    m_chkFullPayload->setToolTip(QStringLiteral("显示完整的消息载荷"));
    m_chkPacket = new QCheckBox(QStringLiteral("打印报文 HEX"), this);
    m_chkPacket->setChecked(true);
    m_chkPacket->setToolTip(QStringLiteral(
        "把收发的 MQTT 原始报文以十六进制转储打印出来，用于排查固件协议问题。\n"
        "报文占用行数较多，不需要排查时可取消勾选。"));
    m_spinRate = new QSpinBox(this);
    m_spinRate->setRange(1, 1000);
    m_spinRate->setValue(20);
    m_spinMax = new QSpinBox(this);
    m_spinMax->setRange(100, 100000);
    m_spinMax->setValue(2000);
    m_btnClear = new QPushButton(QStringLiteral("清空"), this);
    m_btnExport = new QPushButton(QStringLiteral("导出"), this);
    m_btnLogPath = new QPushButton(QStringLiteral("日志路径…"), this);
    m_labStat = new QLabel(QStringLiteral("消息 0 条（累计 0 条）"), this);
    m_labLogPath = new QLabel(QStringLiteral("未启用"), this);

    toolBar->addWidget(m_chkIn);
    toolBar->addWidget(m_chkOut);
    toolBar->addWidget(m_chkAutoScroll);
    toolBar->addWidget(m_chkLogFile);
    toolBar->addWidget(m_chkFullPayload);
    toolBar->addWidget(m_chkPacket);
    toolBar->addWidget(new QLabel(QStringLiteral("每秒上限:"), this));
    toolBar->addWidget(m_spinRate);
    toolBar->addWidget(new QLabel(QStringLiteral("最大行数:"), this));
    toolBar->addWidget(m_spinMax);
    toolBar->addWidget(m_btnClear);
    toolBar->addWidget(m_btnExport);
    toolBar->addWidget(m_btnLogPath);
    toolBar->addStretch();
    toolBar->addWidget(m_labStat);
    toolBar->addWidget(m_labLogPath);

    m_logEdit = new QPlainTextEdit(this);
    m_logEdit->setReadOnly(true);
    QFont font("Consolas");
    font.setStyleHint(QFont::Monospace);
    m_logEdit->setFont(font);
    m_logEdit->setMaximumBlockCount(m_spinMax->value());

    mainLayout->addLayout(toolBar);
    mainLayout->addWidget(m_logEdit, 1);

    connect(m_btnClear, &QPushButton::clicked, this, &LogPanelWidget::clearLog);
    connect(m_btnExport, &QPushButton::clicked, this, &LogPanelWidget::onExportClicked);
    connect(m_btnLogPath, &QPushButton::clicked, this, &LogPanelWidget::onLogPathClicked);
    connect(m_spinMax, QOverload<int>::of(&QSpinBox::valueChanged), this, &LogPanelWidget::onOptionChanged);
    connect(m_chkIn, &QCheckBox::stateChanged, this, &LogPanelWidget::onOptionChanged);
    connect(m_chkOut, &QCheckBox::stateChanged, this, &LogPanelWidget::onOptionChanged);
    connect(m_chkAutoScroll, &QCheckBox::stateChanged, this, &LogPanelWidget::onOptionChanged);
    connect(m_chkLogFile, &QCheckBox::stateChanged, this, &LogPanelWidget::onLogFileToggled);
    connect(m_chkFullPayload, &QCheckBox::stateChanged, this, &LogPanelWidget::onOptionChanged);

    // 兜底定时器：限流的「已省略 N 条」汇总原本只在收到下一条消息且跨秒时才输出。
    // 若突发后消息就停了，被省略的计数永远不会显示，看起来像丢数据。
    // 这里每秒检查一次，把残留的省略计数补写出来。
    m_flushTimer = new QTimer(this);
    m_flushTimer->setInterval(1000);
    connect(m_flushTimer, &QTimer::timeout, this, &LogPanelWidget::onFlushTimer);
    m_flushTimer->start();
}

void LogPanelWidget::onFlushTimer()
{
    if (m_suppressed <= 0)
        return;
    // 只有确实跨过了打印时所处的那一秒才补写，避免与 appendMessage 里的换秒逻辑重复
    if (QTime::currentTime().second() == m_lastSecond)
        return;
    flushSuppressed();
    m_secCount   = 0;
    m_lastSecond = -1;
}

bool LogPanelWidget::setLogFile(const QString &path)
{
    if (m_logFile.isOpen()) {
        m_logStream.flush();
        m_logFile.close();
    }

    if (path.isEmpty()) {
        m_logFilePath.clear();
        m_labLogPath->setText(QStringLiteral("未启用"));
        return true;
    }

    m_logFile.setFileName(path);
    if (!m_logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        appendLog(QStringLiteral("无法打开日志文件 %1").arg(path), 2);
        return false;
    }

    m_logFilePath = path;
    m_logStream.setDevice(&m_logFile);
    m_logStream.setCodec("UTF-8");
    m_labLogPath->setText(QFontMetrics(m_labLogPath->font()).elidedText(path, Qt::ElideLeft, 40));
    return true;
}

QString LogPanelWidget::logFilePath() const
{
    return m_logFilePath;
}

bool LogPanelWidget::isLogFileEnabled() const
{
    return !m_logFilePath.isEmpty();
}

void LogPanelWidget::appendLog(const QString &text, int level)
{
    QString prefix;
    switch (level) {
    case 0: prefix = QStringLiteral("[信息]"); break;
    case 1: prefix = QStringLiteral("[警告]"); break;
    case 2: prefix = QStringLiteral("[错误]"); break;
    default: prefix = QStringLiteral("[未知]"); break;
    }
    QString color;
    switch (level) {
    case 0: color = QStringLiteral("#000000"); break;
    case 1: color = QStringLiteral("#ef6c00"); break;
    case 2: color = QStringLiteral("#c62828"); break;
    default: color = QStringLiteral("#000000"); break;
    }
    QString html = QStringLiteral("<font color=\"%1\">%2 %3</font>").arg(color).arg(QDateTime::currentDateTime().toString("HH:mm:ss")).arg(prefix);
    html += QStringLiteral(" ") + text.toHtmlEscaped();
    writeLine(html);

    QString plainText = QStringLiteral("%1 %2 %3").arg(QDateTime::currentDateTime().toString("HH:mm:ss")).arg(prefix).arg(text);
    appendToLogFile(plainText);
}

void LogPanelWidget::appendMessage(const Mqtt::MessageRecord &rec)
{
    if ((rec.direction == QStringLiteral("IN") && !m_chkIn->isChecked()) ||
        (rec.direction == QStringLiteral("OUT") && !m_chkOut->isChecked())) {
        m_msgTotal++;
        return;
    }

    m_msgTotal++;

    int currentSecond = rec.timestamp.time().second();
    if (currentSecond != m_lastSecond) {
        flushSuppressed();
        m_secCount = 0;
        m_suppressed = 0;
        m_lastSecond = currentSecond;
    }

    if (m_secCount >= m_spinRate->value()) {
        m_suppressed++;
        m_lastSuppressedTopic = rec.topic;
        return;
    }

    m_secCount++;
    m_shown++;

    QString directionMark = rec.direction == QStringLiteral("IN") ? QStringLiteral("↑") : QStringLiteral("↓");
    QString directionColor = rec.direction == QStringLiteral("IN") ? QStringLiteral("#2e7d32") : QStringLiteral("#c62828");
    QString retainMark = rec.retain ? QStringLiteral("<font color=\"#c62828\">[保留]</font>") : QString();
    QString payloadText = escapePayload(rec.payload);
    QString qosText = Mqtt::qosToString(rec.qos);
    QString clientText = rec.clientId.isEmpty() ? QStringLiteral("-") : rec.clientId;

    QString html = QStringLiteral("<font color=\"%1\">%2 %3</font> <b>%4</b> <b>QoS%5</b> 来自 %6 | %7 %8")
            .arg(directionColor)
            .arg(rec.timestamp.toString("HH:mm:ss.zzz"))
            .arg(directionMark)
            .arg(rec.topic)
            .arg(qosText)
            .arg(clientText)
            .arg(payloadText)
            .arg(retainMark);

    writeLine(html);

    // 落盘用纯文本：不带 HTML 标签，载荷也不做 HTML 转义
    const QString plainText =
        QStringLiteral("%1 [消息%2] %3 QoS%4 来自 %5 | %6%7")
            .arg(rec.timestamp.toString(QStringLiteral("HH:mm:ss.zzz")))
            .arg(directionMark)
            .arg(rec.topic)
            .arg(qosText)
            .arg(clientText)
            .arg(plainPayload(rec.payload))
            .arg(rec.retain ? QStringLiteral(" [保留]") : QString());
    appendToLogFile(plainText);
}

void LogPanelWidget::appendPacket(const QString &clientId, const QString &direction,
                                  const QByteArray &raw)
{
    if (!m_chkPacket || !m_chkPacket->isChecked())
        return;
    if (raw.isEmpty())
        return;

    // 与消息共用每秒上限，避免高频遥测把报文刷爆界面
    const int sec = QTime::currentTime().second();
    if (m_lastSecond < 0)
        m_lastSecond = sec;
    if (sec != m_lastSecond) {
        flushSuppressed();
        m_lastSecond = sec;
        m_secCount   = 0;
    }
    const int limit = m_spinRate ? m_spinRate->value() : 20;
    if (m_secCount >= limit) {
        m_suppressed++;
        m_lastSuppressedTopic = QStringLiteral("报文");
        return;
    }
    m_secCount++;
    m_shown++;

    const quint8 header = static_cast<quint8>(raw.at(0));
    const int    type   = Mqtt::typeOf(header);
    const quint8 flags  = Mqtt::flagsOf(header);
    const bool   isIn   = (direction == QLatin1String("IN"));

    const QString ts    = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"));
    const QString arrow = isIn ? QStringLiteral("↑") : QStringLiteral("↓");
    const QString color = isIn ? QStringLiteral("#2e7d32") : QStringLiteral("#c62828");
    const QString dump  = Mqtt::hexDump(raw, 256, 16);

    // 标题行
    writeLine(QStringLiteral("%1 <font color=\"%2\">[报文%3]</font> <b>%4</b> %5 %6 字节 来自 %7")
                  .arg(ts).arg(color).arg(arrow)
                  .arg(Mqtt::packetTypeName(type).toHtmlEscaped())
                  .arg(Mqtt::packetFlagsText(type, flags).toHtmlEscaped())
                  .arg(raw.size())
                  .arg(clientId.toHtmlEscaped()));

    // 十六进制转储（逐行缩进，等宽字体下对齐）
    const QStringList lines = dump.split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i) {
        writeLine(QStringLiteral("<font color=\"#555555\">        %1</font>")
                      .arg(lines.at(i).toHtmlEscaped()));
    }

    // 落盘：标题行 + 转储，纯文本
    appendToLogFile(QStringLiteral("%1 [报文%2] %3 %4 %5 字节 来自 %6\n%7")
                        .arg(ts).arg(arrow)
                        .arg(Mqtt::packetTypeName(type))
                        .arg(Mqtt::packetFlagsText(type, flags))
                        .arg(raw.size())
                        .arg(clientId)
                        .arg(dump));
}

void LogPanelWidget::clearLog()
{
    m_logEdit->clear();
    m_shown    = 0;
    m_msgTotal = 0;
    m_secCount = 0;
    m_suppressed = 0;
    m_lastSecond = -1;
    m_lastSuppressedTopic.clear();
    m_labStat->setText(QStringLiteral("消息 0 条（累计 0 条）"));
}

void LogPanelWidget::onClearClicked()
{
    clearLog();
}

void LogPanelWidget::onExportClicked()
{
    QString fileName = QFileDialog::getSaveFileName(this, QStringLiteral("导出日志"), QStringLiteral("mqtt_log.txt"), QStringLiteral("文本文件 (*.txt)"));
    if (fileName.isEmpty()) {
        return;
    }

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, QStringLiteral("导出失败"), QStringLiteral("无法写入文件 %1").arg(fileName));
        return;
    }

    QTextStream out(&file);
    out.setCodec("UTF-8");
    out << m_logEdit->toPlainText();
    file.close();
}

void LogPanelWidget::onOptionChanged()
{
    m_logEdit->setMaximumBlockCount(m_spinMax->value());
    m_labStat->setText(QStringLiteral("消息 %1 条（累计 %2 条）").arg(m_shown).arg(m_msgTotal));
}

void LogPanelWidget::writeLine(const QString &html)
{
    m_logEdit->appendHtml(html);
    if (m_chkAutoScroll->isChecked()) {
        QScrollBar *scrollBar = m_logEdit->verticalScrollBar();
        scrollBar->setValue(scrollBar->maximum());
    }
    m_labStat->setText(QStringLiteral("消息 %1 条（累计 %2 条）").arg(m_shown).arg(m_msgTotal));
}

void LogPanelWidget::flushSuppressed()
{
    if (m_suppressed > 0) {
        QString html = QStringLiteral("<font color=\"#888888\">… 已省略 %1 条消息（最近主题：%2）</font>")
                .arg(m_suppressed)
                .arg(m_lastSuppressedTopic);
        writeLine(html);
        appendToLogFile(QStringLiteral("… 已省略 %1 条消息（最近主题：%2）")
                            .arg(m_suppressed).arg(m_lastSuppressedTopic));
        m_suppressed = 0;
    }
}

QString LogPanelWidget::escapePayload(const QByteArray &payload) const
{
    if (payload.isEmpty())
        return QStringLiteral("(空)");

    QString str = QString::fromUtf8(payload);
    str.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
    str.replace(QLatin1Char('\r'), QStringLiteral("\\r"));
    str.replace(QLatin1Char('\t'), QStringLiteral("\\t"));

    if (!m_chkFullPayload->isChecked() && str.length() > 200) {
        str = str.left(200);
        str += QStringLiteral(" …（共 %1 字节）").arg(payload.size());
    }
    return str.toHtmlEscaped();
}

QString LogPanelWidget::plainPayload(const QByteArray &payload) const
{
    if (payload.isEmpty())
        return QStringLiteral("(空)");

    QString str = QString::fromUtf8(payload);
    str.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
    str.replace(QLatin1Char('\r'), QStringLiteral("\\r"));
    str.replace(QLatin1Char('\t'), QStringLiteral("\\t"));

    // 未勾选「完整载荷」时截断，与界面显示保持一致
    if ((!m_chkFullPayload || !m_chkFullPayload->isChecked()) && str.length() > 200) {
        str = str.left(200);
        str += QStringLiteral(" …（共 %1 字节）").arg(payload.size());
    }
    return str;   // 落盘是纯文本，不做 HTML 转义
}

void LogPanelWidget::appendToLogFile(const QString &plainText)
{
    if (m_logFile.isOpen()) {
        m_logStream << plainText << '\n';
        m_logStream.flush();
    }
}

void LogPanelWidget::onLogFileToggled(bool on)
{
    if (on) {
        QString fileName = QFileDialog::getSaveFileName(this, QStringLiteral("选择日志文件"), QStringLiteral("mqtt_log.txt"), QStringLiteral("文本文件 (*.txt);;所有文件 (*.*)"));
        if (!fileName.isEmpty()) {
            setLogFile(fileName);
        } else {
            m_chkLogFile->setChecked(false);
        }
    } else {
        setLogFile(QString());
    }
}

void LogPanelWidget::onLogPathClicked()
{
    if (m_logFilePath.isEmpty()) {
        return;
    }

    QString fileName = QFileDialog::getSaveFileName(this, QStringLiteral("选择日志文件"), m_logFilePath, QStringLiteral("文本文件 (*.txt);;所有文件 (*.*)"));
    if (!fileName.isEmpty()) {
        setLogFile(fileName);
    }
}

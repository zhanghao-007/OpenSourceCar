#include "CommandWidget.h"
#include "../core/CoordinateSystem.h"

#include <QComboBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QLabel>
#include <QCheckBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QDateTime>
#include <QMessageBox>
#include <QFont>

CommandWidget::CommandWidget(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
}

void CommandWidget::buildUi()
{
    QVBoxLayout *main = new QVBoxLayout(this);
    main->setContentsMargins(6, 6, 6, 6);
    main->setSpacing(6);

    // ---- 目标与主题 ----
    QHBoxLayout *row1 = new QHBoxLayout();
    row1->addWidget(new QLabel(QStringLiteral("目标小车："), this));
    m_cmbTarget = new QComboBox(this);
    m_cmbTarget->setEditable(true);
    m_cmbTarget->setMinimumWidth(180);
    m_cmbTarget->addItem(QStringLiteral("（全部广播）"));
    row1->addWidget(m_cmbTarget);

    row1->addWidget(new QLabel(QStringLiteral("主题："), this));
    m_editTopic = new QLineEdit(this);
    m_editTopic->setPlaceholderText(QStringLiteral("car/CAR-001/cmd"));
    row1->addWidget(m_editTopic, 1);

    row1->addWidget(new QLabel(QStringLiteral("QoS："), this));
    m_cmbQos = new QComboBox(this);
    m_cmbQos->addItem(QStringLiteral("0 - 至多一次"));
    m_cmbQos->addItem(QStringLiteral("1 - 至少一次"));
    m_cmbQos->addItem(QStringLiteral("2 - 只有一次"));
    m_cmbQos->setCurrentIndex(1);
    row1->addWidget(m_cmbQos);

    m_chkRetain = new QCheckBox(QStringLiteral("保留"), this);
    row1->addWidget(m_chkRetain);

    m_btnSend = new QPushButton(QStringLiteral("下发指令"), this);
    m_btnSend->setMinimumWidth(110);
    m_btnSend->setDefault(true);
    row1->addWidget(m_btnSend);
    main->addLayout(row1);

    // ---- 主体：左快捷指令 / 右载荷 ----
    QHBoxLayout *body = new QHBoxLayout();

    QGroupBox *quickBox = new QGroupBox(QStringLiteral("常用指令"), this);
    QGridLayout *grid = new QGridLayout(quickBox);
    addQuickButton(grid, 0, 0, QStringLiteral("前进"),   QStringLiteral("MOVE"),
                   QStringLiteral("\"v\":0.5,\"w\":0"));
    addQuickButton(grid, 0, 1, QStringLiteral("后退"),   QStringLiteral("MOVE"),
                   QStringLiteral("\"v\":-0.3,\"w\":0"));
    addQuickButton(grid, 1, 0, QStringLiteral("左转"),   QStringLiteral("MOVE"),
                   QStringLiteral("\"v\":0.2,\"w\":0.6"));
    addQuickButton(grid, 1, 1, QStringLiteral("右转"),   QStringLiteral("MOVE"),
                   QStringLiteral("\"v\":0.2,\"w\":-0.6"));
    addQuickButton(grid, 2, 0, QStringLiteral("停止"),   QStringLiteral("STOP"),
                   QString());
    addQuickButton(grid, 2, 1, QStringLiteral("急停"),   QStringLiteral("EMERGENCY_STOP"),
                   QString());
    addQuickButton(grid, 3, 0, QStringLiteral("查询状态"), QStringLiteral("QUERY_STATUS"),
                   QString());
    addQuickButton(grid, 3, 1, QStringLiteral("回充电桩"), QStringLiteral("GO_HOME"),
                   QString());
    addQuickButton(grid, 4, 0, QStringLiteral("设置限速"), QStringLiteral("SET_CFG"),
                   QStringLiteral("\"maxSpeed\":0.8"));
    addQuickButton(grid, 4, 1, QStringLiteral("清零里程"), QStringLiteral("RESET_ODO"),
                   QString());

    // ---- 目标经纬度导航 ----
    QLabel *navTitle = new QLabel(QStringLiteral("目标经纬度导航"), quickBox);
    QFont bold = navTitle->font();
    bold.setBold(true);
    navTitle->setFont(bold);
    grid->addWidget(navTitle, 5, 0, 1, 2);

    grid->addWidget(new QLabel(QStringLiteral("纬度"), quickBox), 6, 0);
    m_editLat = new QLineEdit(quickBox);
    m_editLat->setPlaceholderText(QStringLiteral("39.908692"));
    grid->addWidget(m_editLat, 6, 1);

    grid->addWidget(new QLabel(QStringLiteral("经度"), quickBox), 7, 0);
    m_editLon = new QLineEdit(quickBox);
    m_editLon->setPlaceholderText(QStringLiteral("116.397499"));
    grid->addWidget(m_editLon, 7, 1);

    grid->addWidget(new QLabel(QStringLiteral("坐标系"), quickBox), 8, 0);
    m_cmbCoordSys = new QComboBox(quickBox);
    m_cmbCoordSys->addItem(Geo::systemName(Geo::CS_WGS84), int(Geo::CS_WGS84));
    m_cmbCoordSys->addItem(Geo::systemName(Geo::CS_GCJ02), int(Geo::CS_GCJ02));
    m_cmbCoordSys->addItem(Geo::systemName(Geo::CS_BD09),  int(Geo::CS_BD09));
    m_cmbCoordSys->setToolTip(QStringLiteral(
        "下发目标点时声明本组坐标所属的坐标系，小车端需按同一坐标系解释"));
    grid->addWidget(m_cmbCoordSys, 8, 1);

    QPushButton *btnNav = new QPushButton(QStringLiteral("生成导航指令"), quickBox);
    connect(btnNav, &QPushButton::clicked, this, &CommandWidget::onNavToCoord);
    grid->addWidget(btnNav, 9, 0, 1, 2);

    quickBox->setMaximumWidth(280);
    body->addWidget(quickBox);

    QVBoxLayout *right = new QVBoxLayout();
    m_labHint = new QLabel(QStringLiteral("载荷（JSON）："), this);
    right->addWidget(m_labHint);
    m_editPayload = new QPlainTextEdit(this);
    QFont mono(QStringLiteral("Consolas"));
    mono.setStyleHint(QFont::Monospace);
    m_editPayload->setFont(mono);
    m_editPayload->setPlainText(
        QStringLiteral("{\n  \"cmd\": \"MOVE\",\n  \"seq\": 1,\n  \"params\": {\"v\": 0.5, \"w\": 0}\n}"));
    right->addWidget(m_editPayload, 2);

    m_history = new QTableWidget(this);
    m_history->setColumnCount(5);
    m_history->setHorizontalHeaderLabels(QStringList()
                                         << QStringLiteral("时间")
                                         << QStringLiteral("主题")
                                         << QStringLiteral("QoS")
                                         << QStringLiteral("保留")
                                         << QStringLiteral("载荷"));
    m_history->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_history->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_history->verticalHeader()->setVisible(false);
    m_history->horizontalHeader()->setStretchLastSection(true);
    right->addWidget(m_history, 1);

    body->addLayout(right, 1);
    main->addLayout(body, 1);

    connect(m_btnSend, &QPushButton::clicked, this, &CommandWidget::onSendClicked);
    connect(m_cmbTarget, SIGNAL(currentIndexChanged(int)), this, SLOT(onTargetChanged()));
    connect(m_cmbTarget, SIGNAL(editTextChanged(QString)), this, SLOT(onTargetChanged()));
    connect(m_editTopic, &QLineEdit::textEdited, this, &CommandWidget::onTopicEdited);
}

void CommandWidget::addQuickButton(QGridLayout *grid, int row, int col,
                                   const QString &text, const QString &cmd,
                                   const QString &params)
{
    QPushButton *btn = new QPushButton(text, this);
    btn->setProperty("cmd", cmd);
    btn->setProperty("params", params);
    connect(btn, &QPushButton::clicked, this, &CommandWidget::onQuickCommand);
    grid->addWidget(btn, row, col);
}

void CommandWidget::setCarIds(const QStringList &ids)
{
    const QString keep = m_cmbTarget->currentText();
    m_cmbTarget->blockSignals(true);
    m_cmbTarget->clear();
    m_cmbTarget->addItem(QStringLiteral("（全部广播）"));
    for (int i = 0; i < ids.size(); ++i)
        m_cmbTarget->addItem(ids.at(i));
    const int idx = m_cmbTarget->findText(keep);
    m_cmbTarget->setCurrentIndex(idx >= 0 ? idx : 0);
    m_cmbTarget->blockSignals(false);
}

void CommandWidget::setTopicPrefix(const QString &prefix)
{
    m_prefix = prefix;
    if (!m_topicManual)
        updateTopicFromTarget();
}

void CommandWidget::updateTopicFromTarget()
{
    QString id = m_cmbTarget->currentText().trimmed();
    if (id == QStringLiteral("（全部广播）") || id.isEmpty())
        id = QStringLiteral("+");
    m_editTopic->setText(QStringLiteral("%1/%2/%3").arg(m_prefix).arg(id).arg(m_cmdSuffix));
}

void CommandWidget::onTargetChanged()
{
    if (!m_topicManual)
        updateTopicFromTarget();
}

void CommandWidget::onTopicEdited()
{
    m_topicManual = true;
}

void CommandWidget::fillPayload(const QString &cmd, const QString &params)
{
    QString paramsBlock = params.isEmpty()
                              ? QStringLiteral("{}")
                              : QStringLiteral("{ %1 }").arg(params);
    const QString json = QStringLiteral("{\n  \"cmd\": \"%1\",\n  \"seq\": %2,\n  \"params\": %3\n}")
                             .arg(cmd).arg(m_seq++).arg(paramsBlock);
    m_editPayload->setPlainText(json);
}

void CommandWidget::onQuickCommand()
{
    QPushButton *btn = qobject_cast<QPushButton *>(sender());
    if (!btn)
        return;
    fillPayload(btn->property("cmd").toString(), btn->property("params").toString());
}

void CommandWidget::onNavToCoord()
{
    bool okLat = false, okLon = false;
    const double lat = m_editLat ? m_editLat->text().trimmed().toDouble(&okLat) : 0.0;
    const double lon = m_editLon ? m_editLon->text().trimmed().toDouble(&okLon) : 0.0;

    if (!okLat || !okLon || !Geo::LatLon(lat, lon).isValid()) {
        QMessageBox::warning(this, QStringLiteral("坐标无效"),
                             QStringLiteral("请填写有效的纬度（-90~90）与经度（-180~180），"
                                            "且不同时为 0。"));
        return;
    }

    const Geo::CoordSystem cs =
        static_cast<Geo::CoordSystem>(m_cmbCoordSys->currentData().toInt());
    const QString params = QStringLiteral("\"lat\":%1,\"lon\":%2,\"coord\":\"%3\"")
                               .arg(lat, 0, 'f', 6)
                               .arg(lon, 0, 'f', 6)
                               .arg(Geo::systemName(cs));
    fillPayload(QStringLiteral("GO_TO"), params);
}

void CommandWidget::onSendClicked()
{
    const QString topic = m_editTopic->text().trimmed();
    if (topic.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("无法下发"),
                             QStringLiteral("请先填写主题"));
        return;
    }
    // 广播时把 "+" 换成 "#" 才能作为发布主题吗？不能 —— 发布主题禁止通配符。
    if (topic.contains(QLatin1Char('+')) || topic.contains(QLatin1Char('#'))) {
        QMessageBox::warning(this, QStringLiteral("无法下发"),
                             QStringLiteral("发布主题不允许包含通配符 + / #。\n"
                                            "请指定具体小车编号，或改用群发主题如 car/all/cmd。"));
        return;
    }

    const QByteArray payload = m_editPayload->toPlainText().toUtf8();
    const Mqtt::QoS qos = static_cast<Mqtt::QoS>(m_cmbQos->currentIndex());
    const bool retain = m_chkRetain->isChecked();

    emit publishRequested(topic, payload, qos, retain);
    appendHistory(topic, payload, qos, retain);
}

void CommandWidget::appendHistory(const QString &topic, const QByteArray &payload,
                                  Mqtt::QoS qos, bool retain)
{
    const int row = m_history->rowCount();
    m_history->insertRow(row);
    m_history->setItem(row, 0, new QTableWidgetItem(
                           QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"))));
    m_history->setItem(row, 1, new QTableWidgetItem(topic));
    m_history->setItem(row, 2, new QTableWidgetItem(Mqtt::qosToString(qos)));
    m_history->setItem(row, 3, new QTableWidgetItem(retain ? QStringLiteral("是")
                                                           : QStringLiteral("-")));
    QString text = QString::fromUtf8(payload);
    text.replace(QLatin1Char('\n'), QStringLiteral(" "));
    m_history->setItem(row, 4, new QTableWidgetItem(text));
    while (m_history->rowCount() > 500)
        m_history->removeRow(0);
    m_history->scrollToBottom();
}

void CommandWidget::onClearHistory()
{
    m_history->setRowCount(0);
}

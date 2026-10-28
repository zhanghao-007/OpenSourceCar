#include "HistoryWidget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QHeaderView>
#include <QFileInfo>

HistoryWidget::HistoryWidget(DatabaseManager *db, QWidget *parent)
    : QWidget(parent), m_db(db)
{
    QVBoxLayout *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(8);

    // ---- 查询条件条 ----
    QHBoxLayout *bar = new QHBoxLayout();
    bar->setSpacing(6);

    m_cmbType = new QComboBox(this);
    m_cmbType->addItem(QStringLiteral("遥测记录"), QStringLiteral("telemetry"));
    m_cmbType->addItem(QStringLiteral("消息记录"), QStringLiteral("messages"));
    m_cmbType->addItem(QStringLiteral("设备数据"), QStringLiteral("device_data"));
    m_cmbType->addItem(QStringLiteral("设备事件"), QStringLiteral("device_event"));
    m_cmbType->setMinimumWidth(100);
    bar->addWidget(m_cmbType);

    m_editFilter = new QLineEdit(this);
    m_editFilter->setPlaceholderText(QStringLiteral("筛选：遥测填小车编号 / 消息填主题关键字"));
    m_editFilter->setMinimumWidth(220);
    bar->addWidget(m_editFilter);

    m_cmbDir = new QComboBox(this);
    m_cmbDir->addItem(QStringLiteral("方向 全部"), QString());
    m_cmbDir->addItem(QStringLiteral("上行 IN"),  QStringLiteral("IN"));
    m_cmbDir->addItem(QStringLiteral("下行 OUT"), QStringLiteral("OUT"));
    bar->addWidget(m_cmbDir);

    m_cmbRange = new QComboBox(this);
    m_cmbRange->addItem(QStringLiteral("最近 1 分钟"),  60);
    m_cmbRange->addItem(QStringLiteral("最近 5 分钟"),  300);
    m_cmbRange->addItem(QStringLiteral("最近 30 分钟"), 1800);
    m_cmbRange->addItem(QStringLiteral("最近 1 小时"),  3600);
    m_cmbRange->addItem(QStringLiteral("最近 6 小时"),  21600);
    m_cmbRange->addItem(QStringLiteral("最近 24 小时"), 86400);
    m_cmbRange->addItem(QStringLiteral("全部"),         -1);
    m_cmbRange->setCurrentIndex(2);   // 默认最近 30 分钟
    bar->addWidget(m_cmbRange);

    bar->addWidget(new QLabel(QStringLiteral("上限"), this));
    m_spinLimit = new QSpinBox(this);
    m_spinLimit->setRange(10, 10000);
    m_spinLimit->setValue(500);
    m_spinLimit->setMaximumWidth(90);
    bar->addWidget(m_spinLimit);

    m_btnQuery = new QPushButton(QStringLiteral("查询"), this);
    m_btnExport= new QPushButton(QStringLiteral("导出 CSV"), this);
    m_btnClear = new QPushButton(QStringLiteral("清空本表"), this);
    bar->addWidget(m_btnQuery);
    bar->addWidget(m_btnExport);
    bar->addWidget(m_btnClear);

    bar->addStretch();
    root->addLayout(bar);

    // ---- 状态栏 ----
    m_labStatus = new QLabel(this);
    m_labStatus->setStyleSheet(QStringLiteral("color:#666;font-size:9px;"));
    root->addWidget(m_labStatus);

    // ---- 结果表 ----
    m_table = new QTableWidget(this);
    m_table->setAlternatingRowColors(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setStretchLastSection(true);
    root->addWidget(m_table, 1);

    connect(m_btnQuery,  &QPushButton::clicked, this, &HistoryWidget::onQueryClicked);
    connect(m_btnExport, &QPushButton::clicked, this, &HistoryWidget::onExportClicked);
    connect(m_btnClear,  &QPushButton::clicked, this, &HistoryWidget::onClearClicked);
    // 切换类型时同步"筛选"占位与方向框可用性
    connect(m_cmbType, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
                const QString kind = currentKind();
                m_cmbDir->setEnabled(kind == QLatin1String("messages"));
                if (kind == QLatin1String("telemetry"))
                    m_editFilter->setPlaceholderText(QStringLiteral("筛选：小车编号（可空）"));
                else if (kind == QLatin1String("messages"))
                    m_editFilter->setPlaceholderText(QStringLiteral("筛选：主题关键字（可空）"));
                else
                    m_editFilter->setPlaceholderText(QStringLiteral("筛选：设备编号 devSn（可空）"));
            });
    m_cmbDir->setEnabled(false);

    // 状态定时刷新（数据库在后台不断写入，行数应实时可见）
    m_statusTimer = new QTimer(this);
    m_statusTimer->setInterval(2000);
    connect(m_statusTimer, &QTimer::timeout, this, &HistoryWidget::refreshStatus);
    m_statusTimer->start();

    refreshStatus();
}

qint64 HistoryWidget::rangeMs() const
{
    const int secs = m_cmbRange->currentData().toInt();
    if (secs <= 0)
        return -1;
    return QDateTime::currentMSecsSinceEpoch() - static_cast<qint64>(secs) * 1000;
}

QString HistoryWidget::currentKind() const
{
    return m_cmbType ? m_cmbType->currentData().toString()
                     : QStringLiteral("telemetry");
}

void HistoryWidget::onQueryClicked()
{
    if (!m_db || !m_db->isOpen()) {
        QMessageBox::warning(this, QStringLiteral("无数据库"),
                             QStringLiteral("数据库未启用：%1").arg(m_db ? m_db->lastError() : QString()));
        return;
    }
    const QString kind = currentKind();
    m_lastKind = kind;
    const int limit = m_spinLimit->value();
    const qint64 since = rangeMs();
    const QString filter = m_editFilter->text().trimmed();

    if (kind == QLatin1String("telemetry")) {
        m_lastTele = m_db->queryTelemetry(filter, since, limit);
        showTelemetry(m_lastTele);
    } else if (kind == QLatin1String("messages")) {
        const QString dir = m_cmbDir->currentData().toString();
        m_lastMsg = m_db->queryMessages(dir, filter, since, limit);
        showMessages(m_lastMsg);
    } else if (kind == QLatin1String("device_data")) {
        m_lastDevData = m_db->queryDeviceData(filter, since, limit);
        showDeviceData(m_lastDevData);
    } else {
        m_lastDevEvent = m_db->queryDeviceEvents(filter, since, limit);
        showDeviceEvents(m_lastDevEvent);
    }
    refreshStatus();
}

void HistoryWidget::showTelemetry(const QList<TelemetryRow> &rows)
{
    m_table->clear();
    m_table->setColumnCount(15);
    QStringList hdr = {
        QStringLiteral("序号"), QStringLiteral("小车"), QStringLiteral("时间"),
        QStringLiteral("速度"), QStringLiteral("电量"), QStringLiteral("航向"),
        QStringLiteral("X"),     QStringLiteral("Y"),   QStringLiteral("纬度"),
        QStringLiteral("经度"),   QStringLiteral("坐标系"), QStringLiteral("温度"),
        QStringLiteral("错误码"), QStringLiteral("状态"), QStringLiteral("载荷(JSON)")
    };
    m_table->setHorizontalHeaderLabels(hdr);
    m_table->setRowCount(rows.size());
    for (int i = 0; i < rows.size(); ++i) {
        const TelemetryRow &r = rows.at(i);
        const QString ts = QDateTime::fromMSecsSinceEpoch(r.tsMs)
                              .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
        int c = 0;
        m_table->setItem(i, c++, new QTableWidgetItem(QString::number(r.id)));
        m_table->setItem(i, c++, new QTableWidgetItem(r.carId));
        m_table->setItem(i, c++, new QTableWidgetItem(ts));
        m_table->setItem(i, c++, new QTableWidgetItem(QString::number(r.speed, 'f', 2)));
        m_table->setItem(i, c++, new QTableWidgetItem(QString::number(r.battery, 'f', 1)));
        m_table->setItem(i, c++, new QTableWidgetItem(QString::number(r.heading, 'f', 1)));
        m_table->setItem(i, c++, new QTableWidgetItem(QString::number(r.x, 'f', 2)));
        m_table->setItem(i, c++, new QTableWidgetItem(QString::number(r.y, 'f', 2)));
        m_table->setItem(i, c++, new QTableWidgetItem(QString::number(r.lat, 'f', 6)));
        m_table->setItem(i, c++, new QTableWidgetItem(QString::number(r.lon, 'f', 6)));
        m_table->setItem(i, c++, new QTableWidgetItem(r.coordSys));
        m_table->setItem(i, c++, new QTableWidgetItem(QString::number(r.temperature, 'f', 1)));
        m_table->setItem(i, c++, new QTableWidgetItem(QString::number(r.errorCode)));
        m_table->setItem(i, c++, new QTableWidgetItem(r.status));
        m_table->setItem(i, c++, new QTableWidgetItem(QString::fromUtf8(r.payload)));
    }
    m_table->resizeColumnsToContents();
}

void HistoryWidget::showMessages(const QList<MessageRow> &rows)
{
    m_table->clear();
    m_table->setColumnCount(8);
    QStringList hdr = {
        QStringLiteral("序号"), QStringLiteral("方向"), QStringLiteral("时间"),
        QStringLiteral("主题"), QStringLiteral("QoS"),  QStringLiteral("保留"),
        QStringLiteral("字节"),  QStringLiteral("载荷")
    };
    m_table->setHorizontalHeaderLabels(hdr);
    m_table->setRowCount(rows.size());
    for (int i = 0; i < rows.size(); ++i) {
        const MessageRow &r = rows.at(i);
        const QString ts = QDateTime::fromMSecsSinceEpoch(r.tsMs)
                              .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
        int c = 0;
        m_table->setItem(i, c++, new QTableWidgetItem(QString::number(r.id)));
        m_table->setItem(i, c++, new QTableWidgetItem(r.direction));
        m_table->setItem(i, c++, new QTableWidgetItem(ts));
        m_table->setItem(i, c++, new QTableWidgetItem(r.topic));
        m_table->setItem(i, c++, new QTableWidgetItem(QString::number(r.qos)));
        m_table->setItem(i, c++, new QTableWidgetItem(r.retain ? QStringLiteral("是") : QStringLiteral("否")));
        m_table->setItem(i, c++, new QTableWidgetItem(QString::number(r.size)));
        m_table->setItem(i, c++, new QTableWidgetItem(QString::fromUtf8(r.payload)));
    }
    m_table->resizeColumnsToContents();
}

void HistoryWidget::showDeviceData(const QList<DeviceDataRow> &rows)
{
    m_table->clear();
    m_table->setColumnCount(12);
    QStringList hdr = {
        QStringLiteral("序号"),   QStringLiteral("时间"),
        QStringLiteral("设备"),   QStringLiteral("mid"),
        QStringLiteral("方法"),   QStringLiteral("设备时间(自报)"),
        QStringLiteral("RSSI"),   QStringLiteral("沉降值"),
        QStringLiteral("倾角X"),  QStringLiteral("倾角Y"),
        QStringLiteral("电压(V)"), QStringLiteral("载荷(JSON)")
    };
    m_table->setHorizontalHeaderLabels(hdr);
    m_table->setRowCount(rows.size());
    for (int i = 0; i < rows.size(); ++i) {
        const DeviceDataRow &r = rows.at(i);
        const QString ts = QDateTime::fromMSecsSinceEpoch(r.tsMs)
                              .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
        int c = 0;
        m_table->setItem(i, c++, new QTableWidgetItem(QString::number(r.id)));
        m_table->setItem(i, c++, new QTableWidgetItem(ts));
        m_table->setItem(i, c++, new QTableWidgetItem(r.devSn));
        m_table->setItem(i, c++, new QTableWidgetItem(QString::number(r.mid)));
        m_table->setItem(i, c++, new QTableWidgetItem(r.method));
        m_table->setItem(i, c++, new QTableWidgetItem(r.devTs.isEmpty()
                                                          ? QStringLiteral("-") : r.devTs));
        m_table->setItem(i, c++, new QTableWidgetItem(QString::number(r.rssi)));
        m_table->setItem(i, c++, new QTableWidgetItem(QString::number(r.settlement, 'f', 3)));
        m_table->setItem(i, c++, new QTableWidgetItem(QString::number(r.angX, 'f', 2)));
        m_table->setItem(i, c++, new QTableWidgetItem(QString::number(r.angY, 'f', 2)));
        m_table->setItem(i, c++, new QTableWidgetItem(QString::number(r.batVol, 'f', 2)));
        m_table->setItem(i, c++, new QTableWidgetItem(QString::fromUtf8(r.payload)));
    }
    m_table->resizeColumnsToContents();
}

void HistoryWidget::showDeviceEvents(const QList<DeviceEventRow> &rows)
{
    m_table->clear();
    m_table->setColumnCount(10);
    QStringList hdr = {
        QStringLiteral("序号"),   QStringLiteral("时间"),
        QStringLiteral("设备"),   QStringLiteral("mid"),
        QStringLiteral("类型"),   QStringLiteral("设备时间(自报)"),
        QStringLiteral("MAC"),    QStringLiteral("IPv4"),
        QStringLiteral("原因"),   QStringLiteral("载荷(JSON)")
    };
    m_table->setHorizontalHeaderLabels(hdr);
    m_table->setRowCount(rows.size());
    for (int i = 0; i < rows.size(); ++i) {
        const DeviceEventRow &r = rows.at(i);
        const QString ts = QDateTime::fromMSecsSinceEpoch(r.tsMs)
                              .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
        int c = 0;
        m_table->setItem(i, c++, new QTableWidgetItem(QString::number(r.id)));
        m_table->setItem(i, c++, new QTableWidgetItem(ts));
        m_table->setItem(i, c++, new QTableWidgetItem(r.devSn));
        m_table->setItem(i, c++, new QTableWidgetItem(QString::number(r.mid)));
        m_table->setItem(i, c++, new QTableWidgetItem(r.type));
        m_table->setItem(i, c++, new QTableWidgetItem(r.devTs.isEmpty()
                                                          ? QStringLiteral("-") : r.devTs));
        m_table->setItem(i, c++, new QTableWidgetItem(r.mac));
        m_table->setItem(i, c++, new QTableWidgetItem(r.ipv4));
        m_table->setItem(i, c++, new QTableWidgetItem(r.reason));
        m_table->setItem(i, c++, new QTableWidgetItem(QString::fromUtf8(r.payload)));
    }
    m_table->resizeColumnsToContents();
}

QString HistoryWidget::csvField(const QString &s) const
{
    // 简单 CSV 转义：含逗号/引号/换行时用双引号包裹并把内部引号翻倍
    if (s.contains(QLatin1Char(',')) || s.contains(QLatin1Char('"'))
            || s.contains(QLatin1Char('\n')) || s.contains(QLatin1Char('\r'))) {
        QString tmp = s;
        tmp.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        return QLatin1Char('"') + tmp + QLatin1Char('"');
    }
    return s;
}

void HistoryWidget::onExportClicked()
{
    const QString kind = m_lastKind;
    int rowCount = 0;
    if (kind == QLatin1String("telemetry"))
        rowCount = m_lastTele.size();
    else if (kind == QLatin1String("messages"))
        rowCount = m_lastMsg.size();
    else if (kind == QLatin1String("device_data"))
        rowCount = m_lastDevData.size();
    else
        rowCount = m_lastDevEvent.size();

    if (rowCount == 0) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("请先查询出数据再导出"));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("导出 CSV"),
        QStringLiteral("history_%1.csv").arg(QDateTime::currentDateTime()
                                                .toString(QStringLiteral("yyyyMMdd_HHmmss"))),
        QStringLiteral("CSV 文件 (*.csv)"));
    if (path.isEmpty())
        return;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, QStringLiteral("导出失败"),
                             QStringLiteral("无法写入文件：%1").arg(path));
        return;
    }
    QTextStream out(&f);
    out.setCodec("UTF-8");
    out.setGenerateByteOrderMark(true);

    if (kind == QLatin1String("telemetry")) {
        out << csvField(QStringLiteral("序号")) << ','
            << csvField(QStringLiteral("小车")) << ','
            << csvField(QStringLiteral("时间")) << ','
            << csvField(QStringLiteral("速度")) << ','
            << csvField(QStringLiteral("电量")) << ','
            << csvField(QStringLiteral("航向")) << ','
            << csvField(QStringLiteral("X")) << ','
            << csvField(QStringLiteral("Y")) << ','
            << csvField(QStringLiteral("纬度")) << ','
            << csvField(QStringLiteral("经度")) << ','
            << csvField(QStringLiteral("坐标系")) << ','
            << csvField(QStringLiteral("温度")) << ','
            << csvField(QStringLiteral("错误码")) << ','
            << csvField(QStringLiteral("状态")) << ','
            << csvField(QStringLiteral("载荷")) << '\n';
        for (const TelemetryRow &r : qAsConst(m_lastTele)) {
            const QString ts = QDateTime::fromMSecsSinceEpoch(r.tsMs)
                                  .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
            out << r.id << ','
                << csvField(r.carId) << ','
                << csvField(ts) << ','
                << r.speed << ',' << r.battery << ',' << r.heading << ','
                << r.x << ',' << r.y << ',' << r.lat << ',' << r.lon << ','
                << csvField(r.coordSys) << ',' << r.temperature << ','
                << r.errorCode << ','
                << csvField(r.status) << ','
                << csvField(QString::fromUtf8(r.payload)) << '\n';
        }
    } else if (kind == QLatin1String("messages")) {
        out << csvField(QStringLiteral("序号")) << ','
            << csvField(QStringLiteral("方向")) << ','
            << csvField(QStringLiteral("时间")) << ','
            << csvField(QStringLiteral("主题")) << ','
            << csvField(QStringLiteral("QoS")) << ','
            << csvField(QStringLiteral("保留")) << ','
            << csvField(QStringLiteral("字节")) << ','
            << csvField(QStringLiteral("载荷")) << '\n';
        for (const MessageRow &r : qAsConst(m_lastMsg)) {
            const QString ts = QDateTime::fromMSecsSinceEpoch(r.tsMs)
                                  .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
            out << r.id << ','
                << csvField(r.direction) << ','
                << csvField(ts) << ','
                << csvField(r.topic) << ','
                << r.qos << ','
                << (r.retain ? 1 : 0) << ','
                << r.size << ','
                << csvField(QString::fromUtf8(r.payload)) << '\n';
        }
    } else if (kind == QLatin1String("device_data")) {
        out << csvField(QStringLiteral("序号")) << ','
            << csvField(QStringLiteral("时间")) << ','
            << csvField(QStringLiteral("设备")) << ','
            << csvField(QStringLiteral("mid")) << ','
            << csvField(QStringLiteral("方法")) << ','
            << csvField(QStringLiteral("设备时间")) << ','
            << csvField(QStringLiteral("RSSI")) << ','
            << csvField(QStringLiteral("沉降值")) << ','
            << csvField(QStringLiteral("倾角X")) << ','
            << csvField(QStringLiteral("倾角Y")) << ','
            << csvField(QStringLiteral("电压V")) << ','
            << csvField(QStringLiteral("载荷")) << '\n';
        for (const DeviceDataRow &r : qAsConst(m_lastDevData)) {
            const QString ts = QDateTime::fromMSecsSinceEpoch(r.tsMs)
                                  .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
            out << r.id << ','
                << csvField(ts) << ','
                << csvField(r.devSn) << ','
                << r.mid << ','
                << csvField(r.method) << ','
                << csvField(r.devTs) << ','
                << r.rssi << ',' << r.settlement << ',' << r.angX << ','
                << r.angY << ',' << r.batVol << ','
                << csvField(QString::fromUtf8(r.payload)) << '\n';
        }
    } else {
        out << csvField(QStringLiteral("序号")) << ','
            << csvField(QStringLiteral("时间")) << ','
            << csvField(QStringLiteral("设备")) << ','
            << csvField(QStringLiteral("mid")) << ','
            << csvField(QStringLiteral("类型")) << ','
            << csvField(QStringLiteral("设备时间")) << ','
            << csvField(QStringLiteral("MAC")) << ','
            << csvField(QStringLiteral("IPv4")) << ','
            << csvField(QStringLiteral("原因")) << ','
            << csvField(QStringLiteral("载荷")) << '\n';
        for (const DeviceEventRow &r : qAsConst(m_lastDevEvent)) {
            const QString ts = QDateTime::fromMSecsSinceEpoch(r.tsMs)
                                  .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
            out << r.id << ','
                << csvField(ts) << ','
                << csvField(r.devSn) << ','
                << r.mid << ','
                << csvField(r.type) << ','
                << csvField(r.devTs) << ','
                << csvField(r.mac) << ','
                << csvField(r.ipv4) << ','
                << csvField(r.reason) << ','
                << csvField(QString::fromUtf8(r.payload)) << '\n';
        }
    }
    f.close();
    QMessageBox::information(this, QStringLiteral("导出完成"),
                             QStringLiteral("已导出 %1 条记录到：\n%2")
                                 .arg(rowCount)
                                 .arg(path));
}

void HistoryWidget::onClearClicked()
{
    if (!m_db || !m_db->isOpen())
        return;
    const QString kind = m_lastKind;
    QString name = QStringLiteral("遥测记录");
    if (kind == QLatin1String("messages"))
        name = QStringLiteral("消息记录");
    else if (kind == QLatin1String("device_data"))
        name = QStringLiteral("设备数据");
    else if (kind == QLatin1String("device_event"))
        name = QStringLiteral("设备事件");

    if (QMessageBox::question(this, QStringLiteral("确认清空"),
            QStringLiteral("确定要删除数据库中的全部「%1」吗？此操作不可恢复。").arg(name),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
        return;

    if (kind == QLatin1String("telemetry"))
        m_db->clearTelemetry();
    else if (kind == QLatin1String("messages"))
        m_db->clearMessages();
    else if (kind == QLatin1String("device_data"))
        m_db->clearDeviceData();
    else
        m_db->clearDeviceEvents();
    onQueryClicked();   // 重新查询（应为空）并刷新状态
}

void HistoryWidget::refreshStatus()
{
    if (!m_db) {
        m_labStatus->setText(QStringLiteral("数据库未启用"));
        return;
    }
    QString sizeStr = QStringLiteral("—");
    if (m_db->isOpen()) {
        QFileInfo fi(m_db->filePath());
        if (fi.exists())
            sizeStr = QStringLiteral("%1 KB").arg(fi.size() / 1024.0, 0, 'f', 1);
    }
    const QString state = m_db->isOpen() ? QStringLiteral("已启用") : QStringLiteral("未启用");
    m_labStatus->setText(
        QStringLiteral("数据库：%1 ｜ 文件：%2 ｜ 大小：%3 ｜ 遥测 %4 条 ｜ 消息 %5 条 "
                       "｜ 设备数据 %6 条 ｜ 设备事件 %7 条")
            .arg(state)
            .arg(m_db->isOpen() ? m_db->filePath() : QStringLiteral("-"))
            .arg(sizeStr)
            .arg(m_db->telemetryCount())
            .arg(m_db->messageCount())
            .arg(m_db->deviceDataCount())
            .arg(m_db->deviceEventCount()));
}

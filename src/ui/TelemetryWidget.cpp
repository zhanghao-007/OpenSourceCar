#include "TelemetryWidget.h"

#include <QTableWidget>
#include <QPushButton>
#include <QLabel>
#include <QSplitter>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPainter>
#include <QDateTime>
#include <QGroupBox>

// ===========================================================================
// SimpleChart
// ===========================================================================
SimpleChart::SimpleChart(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(180);
    setAttribute(Qt::WA_StyledBackground, true);
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, Qt::white);
    setPalette(pal);
}

void SimpleChart::setSpeedSeries(const QVector<double> &v)   { m_speed = v;   update(); }
void SimpleChart::setBatterySeries(const QVector<double> &v) { m_battery = v; update(); }
void SimpleChart::setTitles(const QString &carId)            { m_carId = carId; update(); }

void SimpleChart::clear()
{
    m_speed.clear();
    m_battery.clear();
    m_carId.clear();
    update();
}

void SimpleChart::drawGrid(QPainter &p, const QRect &plot)
{
    p.save();
    p.setPen(QPen(QColor(230, 230, 230), 1, Qt::SolidLine));
    for (int i = 0; i <= 4; ++i) {
        const int y = plot.bottom() - plot.height() * i / 4;
        p.drawLine(plot.left(), y, plot.right(), y);
    }
    for (int i = 0; i <= 6; ++i) {
        const int x = plot.left() + plot.width() * i / 6;
        p.drawLine(x, plot.top(), x, plot.bottom());
    }
    p.setPen(QPen(QColor(160, 160, 160), 1, Qt::SolidLine));
    p.drawRect(plot);
    p.restore();
}

void SimpleChart::drawSeries(QPainter &p, const QVector<double> &data,
                             const QColor &color, double minV, double maxV,
                             const QRect &plot)
{
    if (data.size() < 2)
        return;

    const double span = (maxV - minV) > 1e-9 ? (maxV - minV) : 1.0;
    QPolygonF poly;
    for (int i = 0; i < data.size(); ++i) {
        const double x = plot.left() + double(plot.width()) * i / double(data.size() - 1);
        double norm = (data.at(i) - minV) / span;
        norm = qBound(0.0, norm, 1.0);
        const double y = plot.bottom() - plot.height() * norm;
        poly.append(QPointF(x, y));
    }
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(color, 2, Qt::SolidLine));
    p.drawPolyline(poly);
    p.restore();
}

void SimpleChart::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), Qt::white);

    QRect plot = rect().adjusted(46, 22, -12, -26);

    // 标题
    p.save();
    p.setPen(QColor(60, 60, 60));
    QFont f = p.font();
    f.setBold(true);
    p.setFont(f);
    p.drawText(QPoint(8, 15), m_carId.isEmpty()
                                  ? QStringLiteral("未选择小车")
                                  : QStringLiteral("小车 %1 遥测曲线（最近 %2 点）")
                                        .arg(m_carId).arg(qMax(m_speed.size(), m_battery.size())));
    p.restore();

    if (plot.width() < 20 || plot.height() < 20)
        return;

    drawGrid(p, plot);

    // 左轴刻度：0~100%（电量共用），速度单独归一化到自身最大值的 1.2 倍
    p.save();
    p.setPen(QColor(120, 120, 120));
    QFont small = p.font();
    small.setPointSize(small.pointSize() - 1);
    p.setFont(small);
    for (int i = 0; i <= 4; ++i) {
        const int y = plot.bottom() - plot.height() * i / 4;
        const double v = 100.0 * i / 4.0;
        p.drawText(QRect(4, y - 7, 38, 14), Qt::AlignRight | Qt::AlignVCenter,
                   QString::number(v, 'f', 0) + QStringLiteral("%"));
    }
    p.restore();

    // 速度：按自身最大值归一化后画在同一坐标系（0~100% 刻度表示占比）
    double speedMax = 0.0;
    for (int i = 0; i < m_speed.size(); ++i)
        speedMax = qMax(speedMax, m_speed.at(i));
    if (speedMax < 1e-9)
        speedMax = 1.0;
    QVector<double> speedNorm;
    for (int i = 0; i < m_speed.size(); ++i)
        speedNorm.append(m_speed.at(i) / speedMax * 100.0);

    drawSeries(p, speedNorm, QColor(220, 60, 60), 0.0, 100.0, plot);   // 速度：红
    drawSeries(p, m_battery, QColor(50, 120, 220), 0.0, 100.0, plot);  // 电量：蓝

    // 图例
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    int lx = plot.left() + 6;
    const int ly = plot.bottom() + 14;
    p.setPen(QPen(QColor(220, 60, 60), 2));
    p.drawLine(lx, ly - 4, lx + 18, ly - 4);
    p.setPen(QColor(60, 60, 60));
    p.drawText(lx + 24, ly, QStringLiteral("速度（峰值 %1 m/s）").arg(speedMax, 0, 'f', 2));
    lx += 190;
    p.setPen(QPen(QColor(50, 120, 220), 2));
    p.drawLine(lx, ly - 4, lx + 18, ly - 4);
    p.setPen(QColor(60, 60, 60));
    p.drawText(lx + 24, ly, QStringLiteral("电量 %"));
    p.restore();
}

// ===========================================================================
// TelemetryWidget
// ===========================================================================
TelemetryWidget::TelemetryWidget(CarRegistry *registry, QWidget *parent)
    : QWidget(parent)
    , m_registry(registry)
{
    buildUi();
}

void TelemetryWidget::buildUi()
{
    QVBoxLayout *main = new QVBoxLayout(this);
    main->setContentsMargins(6, 6, 6, 6);
    main->setSpacing(6);

    QHBoxLayout *bar = new QHBoxLayout();
    bar->addWidget(new QLabel(QStringLiteral("失联判定时限："), this));
    m_spinTimeout = new QSpinBox(this);
    m_spinTimeout->setRange(5, 3600);
    m_spinTimeout->setSuffix(QStringLiteral(" 秒"));
    m_spinTimeout->setValue(m_timeoutSec);
    bar->addWidget(m_spinTimeout);

    bar->addWidget(new QLabel(QStringLiteral("低电量告警："), this));
    m_spinBattery = new QDoubleSpinBox(this);
    m_spinBattery->setRange(0.0, 100.0);
    m_spinBattery->setSuffix(QStringLiteral(" %"));
    m_spinBattery->setValue(m_lowBattery);
    bar->addWidget(m_spinBattery);

    bar->addWidget(new QLabel(QStringLiteral("坐标显示为："), this));
    m_cmbCoord = new QComboBox(this);
    // userData: -1 = 跟随小车上报的原始坐标系；0/1/2 = 强制转换到对应坐标系
    m_cmbCoord->addItem(QStringLiteral("跟随小车上报"), -1);
    m_cmbCoord->addItem(Geo::systemName(Geo::CS_WGS84), int(Geo::CS_WGS84));
    m_cmbCoord->addItem(QStringLiteral("%1（火星坐标）").arg(Geo::systemName(Geo::CS_GCJ02)),
                        int(Geo::CS_GCJ02));
    m_cmbCoord->addItem(QStringLiteral("%1（百度坐标）").arg(Geo::systemName(Geo::CS_BD09)),
                        int(Geo::CS_BD09));
    m_cmbCoord->setToolTip(QStringLiteral(
        "小车上报的坐标系由遥测报文里的 coord 字段声明（缺省 WGS-84）。\n"
        "选「跟随小车上报」则原样显示；选其它则自动换算后显示。"));
    m_cmbCoord->setCurrentIndex(1);   // 默认统一显示为 WGS-84
    bar->addWidget(m_cmbCoord);

    m_btnClear = new QPushButton(QStringLiteral("清空曲线"), this);
    bar->addWidget(m_btnClear);
    bar->addStretch();

    m_labAlarm = new QLabel(QStringLiteral("告警：无"), this);
    bar->addWidget(m_labAlarm);
    main->addLayout(bar);

    QSplitter *splitter = new QSplitter(Qt::Vertical, this);

    m_table = new QTableWidget(splitter);
    m_table->setColumnCount(12);
    refreshCoordHeaders();
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(true);

    QGroupBox *chartBox = new QGroupBox(QStringLiteral("遥测曲线"), splitter);
    QVBoxLayout *chartLayout = new QVBoxLayout(chartBox);
    m_labDetail = new QLabel(QStringLiteral("未选择小车"), chartBox);
    m_chart     = new SimpleChart(chartBox);
    chartLayout->addWidget(m_labDetail);
    chartLayout->addWidget(m_chart, 1);

    splitter->addWidget(m_table);
    splitter->addWidget(chartBox);
    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 1);

    main->addWidget(splitter, 1);

    connect(m_table, &QTableWidget::itemSelectionChanged,
            this, &TelemetryWidget::onSelectionChanged);
    connect(m_btnClear, &QPushButton::clicked, this, &TelemetryWidget::clearHistory);
    connect(m_spinTimeout, SIGNAL(valueChanged(int)), this, SLOT(onThresholdChanged()));
    connect(m_spinBattery, SIGNAL(valueChanged(double)), this, SLOT(onThresholdChanged()));
    connect(m_cmbCoord, SIGNAL(currentIndexChanged(int)), this, SLOT(onCoordSystemChanged()));
}

Geo::LatLon TelemetryWidget::coordFor(const QString &carId) const
{
    if (!m_registry)
        return Geo::LatLon();

    const CarState st = m_registry->car(carId);
    const Geo::LatLon raw(st.lat, st.lon);
    if (!st.gpsValid)
        return raw;

    const int mode = m_cmbCoord ? m_cmbCoord->currentData().toInt() : int(Geo::CS_WGS84);
    if (mode < 0)                     // 跟随小车上报，不做换算
        return raw;
    return Geo::convert(raw, st.coordSys, static_cast<Geo::CoordSystem>(mode));
}

QString TelemetryWidget::coordHeader() const
{
    const int mode = m_cmbCoord ? m_cmbCoord->currentData().toInt() : int(Geo::CS_WGS84);
    if (mode < 0)
        return QStringLiteral("原值");
    return Geo::systemName(static_cast<Geo::CoordSystem>(mode));
}

void TelemetryWidget::refreshCoordHeaders()
{
    const QString cs = coordHeader();
    m_table->setHorizontalHeaderLabels(QStringList()
                                       << QStringLiteral("小车编号")
                                       << QStringLiteral("状态")
                                       << QStringLiteral("速度 m/s")
                                       << QStringLiteral("电量 %")
                                       << QStringLiteral("航向 °")
                                       << QStringLiteral("纬度(%1)").arg(cs)
                                       << QStringLiteral("经度(%1)").arg(cs)
                                       << QStringLiteral("X / m")
                                       << QStringLiteral("Y / m")
                                       << QStringLiteral("温度 ℃")
                                       << QStringLiteral("错误码")
                                       << QStringLiteral("最后上报"));
}

void TelemetryWidget::onCoordSystemChanged()
{
    // 坐标系切换只影响显示，立即更新表头、表格与详情
    refreshCoordHeaders();
    refreshTable();
    if (!m_current.isEmpty())
        updateChartFor(m_current);
}

QColor TelemetryWidget::statusColor(const CarState &st) const
{
    const qint64 idle = st.lastUpdate.isValid()
                            ? st.lastUpdate.secsTo(QDateTime::currentDateTime())
                            : 99999;
    if (idle > m_timeoutSec)
        return QColor(230, 230, 230);          // 失联：灰
    if (st.errorCode != 0)
        return QColor(255, 205, 205);          // 故障：红
    if (st.battery < m_lowBattery)
        return QColor(255, 236, 200);          // 低电量：橙
    return QColor(215, 245, 215);              // 正常：绿
}

void TelemetryWidget::refreshTable()
{
    if (!m_registry)
        return;

    const QString keep = m_current;
    const QStringList ids = m_registry->carIds();
    m_table->setRowCount(ids.size());

    int alarmCount = 0;
    for (int i = 0; i < ids.size(); ++i) {
        const CarState st = m_registry->car(ids.at(i));
        const qint64 idle = st.lastUpdate.isValid()
                                ? st.lastUpdate.secsTo(QDateTime::currentDateTime())
                                : 99999;

        QString status = st.status;
        if (idle > m_timeoutSec)
            status = QStringLiteral("失联");
        else if (!st.online)
            status = QStringLiteral("离线");

        // 经纬度按界面选定的显示坐标系换算
        const Geo::LatLon pos = coordFor(st.carId);

        m_table->setItem(i, 0, new QTableWidgetItem(st.carId));
        m_table->setItem(i, 1, new QTableWidgetItem(status));
        m_table->setItem(i, 2, new QTableWidgetItem(QString::number(st.speed, 'f', 2)));
        m_table->setItem(i, 3, new QTableWidgetItem(QString::number(st.battery, 'f', 1)));
        m_table->setItem(i, 4, new QTableWidgetItem(QString::number(st.heading, 'f', 1)));
        m_table->setItem(i, 5, new QTableWidgetItem(
                             st.gpsValid ? QString::number(pos.lat, 'f', 6)
                                         : QStringLiteral("无定位")));
        m_table->setItem(i, 6, new QTableWidgetItem(
                             st.gpsValid ? QString::number(pos.lon, 'f', 6)
                                         : QStringLiteral("无定位")));
        m_table->setItem(i, 7, new QTableWidgetItem(QString::number(st.x, 'f', 2)));
        m_table->setItem(i, 8, new QTableWidgetItem(QString::number(st.y, 'f', 2)));
        m_table->setItem(i, 9, new QTableWidgetItem(QString::number(st.temperature, 'f', 1)));
        m_table->setItem(i, 10, new QTableWidgetItem(QString::number(st.errorCode)));
        m_table->setItem(i, 11, new QTableWidgetItem(
                             st.lastUpdate.isValid()
                                 ? st.lastUpdate.toString(QStringLiteral("HH:mm:ss"))
                                 : QStringLiteral("-")));

        const QColor bg = statusColor(st);
        for (int c = 0; c < m_table->columnCount(); ++c) {
            QTableWidgetItem *it = m_table->item(i, c);
            if (it)
                it->setBackground(bg);
        }

        if (idle > m_timeoutSec || st.errorCode != 0 || st.battery < m_lowBattery)
            alarmCount++;
    }

    m_labAlarm->setText(alarmCount == 0
                            ? QStringLiteral("告警：无")
                            : QStringLiteral("告警：%1 台小车需要关注").arg(alarmCount));
    m_labAlarm->setStyleSheet(alarmCount == 0
                                  ? QStringLiteral("color:#2e7d32;")
                                  : QStringLiteral("color:#c62828;font-weight:bold;"));

    // 恢复选中
    if (!keep.isEmpty()) {
        for (int i = 0; i < m_table->rowCount(); ++i) {
            if (m_table->item(i, 0) && m_table->item(i, 0)->text() == keep) {
                m_table->selectRow(i);
                break;
            }
        }
    }
    if (m_table->currentRow() >= 0)
        updateChartFor(m_current);
}

void TelemetryWidget::onCarUpdated(const QString &carId)
{
    // 高频遥测下不做整表重排，只更新当前选中车的曲线
    if (carId == m_current)
        updateChartFor(carId);
}

void TelemetryWidget::onSelectionChanged()
{
    const int row = m_table->currentRow();
    if (row < 0 || !m_table->item(row, 0)) {
        m_current.clear();
        return;
    }
    m_current = m_table->item(row, 0)->text();
    updateChartFor(m_current);
}

void TelemetryWidget::updateChartFor(const QString &carId)
{
    if (!m_registry || carId.isEmpty())
        return;
    const CarState st = m_registry->car(carId);
    m_chart->setTitles(carId);
    m_chart->setSpeedSeries(st.speedHistory);
    m_chart->setBatterySeries(st.batteryHistory);

    const Geo::LatLon pos = coordFor(carId);
    m_labDetail->setText(
        QStringLiteral("小车 %1    速度 %2 m/s    电量 %3%%    本地坐标 (%4, %5)    "
                       "经纬度 %6 [%7]    温度 %8℃    错误码 %9")
            .arg(st.carId)
            .arg(st.speed, 0, 'f', 2)
            .arg(st.battery, 0, 'f', 1)
            .arg(st.x, 0, 'f', 2)
            .arg(st.y, 0, 'f', 2)
            .arg(Geo::format(pos))
            .arg(coordHeader())
            .arg(st.temperature, 0, 'f', 1)
            .arg(st.errorCode));
}

void TelemetryWidget::onThresholdChanged()
{
    m_timeoutSec = m_spinTimeout->value();
    m_lowBattery = m_spinBattery->value();
    refreshTable();
}

void TelemetryWidget::clearHistory()
{
    if (m_registry)
        m_registry->clearHistory();
    m_chart->clear();
}

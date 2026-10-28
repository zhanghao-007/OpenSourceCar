#ifndef TELEMETRYWIDGET_H
#define TELEMETRYWIDGET_H

#include "../core/CarRegistry.h"

#include <QWidget>
#include <QVector>
#include <QHash>

QT_BEGIN_NAMESPACE
class QTableWidget;
class QPushButton;
class QLabel;
class QSplitter;
class QSpinBox;
class QDoubleSpinBox;
class QComboBox;
QT_END_NAMESPACE

// ---------------------------------------------------------------------------
// 曲线控件：自绘折线，不依赖 QtCharts 模块
// ---------------------------------------------------------------------------
class SimpleChart : public QWidget
{
    Q_OBJECT
public:
    explicit SimpleChart(QWidget *parent = nullptr);

    void setSpeedSeries(const QVector<double> &v);
    void setBatterySeries(const QVector<double> &v);
    void setTitles(const QString &carId);
    void clear();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void drawSeries(QPainter &p, const QVector<double> &data,
                    const QColor &color, double minV, double maxV,
                    const QRect &plot);
    void drawGrid(QPainter &p, const QRect &plot);

    QVector<double> m_speed;
    QVector<double> m_battery;
    QString         m_carId;
};

// ---------------------------------------------------------------------------
// 遥测看板：小车状态表 + 选中车的速度/电量曲线
// ---------------------------------------------------------------------------
class TelemetryWidget : public QWidget
{
    Q_OBJECT
public:
    explicit TelemetryWidget(CarRegistry *registry, QWidget *parent = nullptr);

public slots:
    void refreshTable();
    void onCarUpdated(const QString &carId);
    void clearHistory();

private slots:
    void onSelectionChanged();
    void onThresholdChanged();
    void onCoordSystemChanged();

private:
    void buildUi();
    void updateChartFor(const QString &carId);
    QColor statusColor(const CarState &st) const;
    // 按界面选定的显示坐标系换算经纬度
    Geo::LatLon coordFor(const QString &carId) const;
    QString     coordHeader() const;   // 表头里标注当前坐标系
    void        refreshCoordHeaders();

    CarRegistry  *m_registry = nullptr;
    QTableWidget *m_table    = nullptr;
    SimpleChart  *m_chart    = nullptr;
    QLabel       *m_labDetail = nullptr;
    QLabel       *m_labAlarm  = nullptr;
    QSpinBox     *m_spinTimeout = nullptr;
    QDoubleSpinBox *m_spinBattery = nullptr;
    QComboBox    *m_cmbCoord  = nullptr;
    QPushButton  *m_btnClear  = nullptr;

    int    m_timeoutSec  = 30;      // 超过该秒数未上报视为失联
    double m_lowBattery  = 20.0;    // 低电量告警阈值
    QString m_current;
};

#endif // TELEMETRYWIDGET_H

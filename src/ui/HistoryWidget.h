#ifndef HISTORYWIDGET_H
#define HISTORYWIDGET_H

#include "../core/DatabaseManager.h"

#include <QWidget>
#include <QComboBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QPushButton>
#include <QTableWidget>
#include <QLabel>
#include <QTimer>

// ---------------------------------------------------------------------------
// HistoryWidget —— 历史数据库浏览页
//
// 从本地 SQLite 数据库里按条件回查"遥测记录"/"消息记录"/"设备数据"/"设备事件"，
// 支持按小车编号 / 设备编号 / 主题关键字 / 时间范围 / 条数上限筛选，
// 可一键导出 CSV，也可清空当前表（保留结构）。
// ---------------------------------------------------------------------------
class HistoryWidget : public QWidget
{
    Q_OBJECT
public:
    explicit HistoryWidget(DatabaseManager *db, QWidget *parent = nullptr);

private slots:
    void onQueryClicked();
    void onExportClicked();
    void onClearClicked();
    void refreshStatus();

private:
    void showTelemetry(const QList<TelemetryRow> &rows);
    void showMessages(const QList<MessageRow> &rows);
    void showDeviceData(const QList<DeviceDataRow> &rows);
    void showDeviceEvents(const QList<DeviceEventRow> &rows);
    QString csvField(const QString &s) const;
    qint64 rangeMs() const;          // 当前时间范围对应的起始毫秒（-1 表示全部）
    QString currentKind() const;     // telemetry / messages / device_data / device_event

    DatabaseManager *m_db = nullptr;

    QComboBox  *m_cmbType   = nullptr;
    QLineEdit  *m_editFilter= nullptr;
    QComboBox  *m_cmbDir    = nullptr;   // 消息方向（仅消息类型可用）
    QComboBox  *m_cmbRange  = nullptr;
    QSpinBox   *m_spinLimit = nullptr;
    QPushButton *m_btnQuery = nullptr;
    QPushButton *m_btnExport= nullptr;
    QPushButton *m_btnClear = nullptr;
    QLabel     *m_labStatus = nullptr;
    QTableWidget *m_table   = nullptr;

    QTimer     *m_statusTimer = nullptr;

    // 最近一次查询结果（供导出使用）
    QList<TelemetryRow>   m_lastTele;
    QList<MessageRow>     m_lastMsg;
    QList<DeviceDataRow>  m_lastDevData;
    QList<DeviceEventRow> m_lastDevEvent;
    QString               m_lastKind = QStringLiteral("telemetry");
};

#endif // HISTORYWIDGET_H

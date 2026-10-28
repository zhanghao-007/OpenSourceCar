#ifndef CLIENTSWIDGET_H
#define CLIENTSWIDGET_H

#include "../mqtt/MqttTypes.h"

#include <QWidget>
#include <QHash>

QT_BEGIN_NAMESPACE
class QTableWidget;
class QListWidget;
class QPushButton;
class QLabel;
class QSplitter;
QT_END_NAMESPACE

// ---------------------------------------------------------------------------
// 客户端（小车）连接管理页
// ---------------------------------------------------------------------------
class ClientsWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ClientsWidget(QWidget *parent = nullptr);

public slots:
    // 由 MainWindow 转发的 Broker 快照
    void onClientsSnapshot(const QList<Mqtt::ClientViewInfo> &list);
    void clearAll();

signals:
    void kickRequested(const QString &clientId);

private slots:
    void onSelectionChanged();
    void onKickClicked();
    void onRefreshClicked();

private:
    void buildUi();
    QString currentClientId() const;
    void    appendRow(int row, const Mqtt::ClientViewInfo &info);

    QTableWidget *m_table      = nullptr;
    QListWidget  *m_subList    = nullptr;
    QPushButton  *m_btnKick    = nullptr;
    QPushButton  *m_btnRefresh = nullptr;
    QLabel       *m_labCount   = nullptr;
    QLabel       *m_labDetail  = nullptr;

    QHash<QString, Mqtt::ClientViewInfo> m_lastInfo;   // 保留上次数据用于详情展示
};

#endif // CLIENTSWIDGET_H

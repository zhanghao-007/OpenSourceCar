#ifndef MESSAGESWIDGET_H
#define MESSAGESWIDGET_H

#include "../mqtt/MqttTypes.h"

#include <QWidget>
#include <QHash>
#include <QSet>
#include <QList>

QT_BEGIN_NAMESPACE
class QTableWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QLineEdit;
class QPushButton;
class QCheckBox;
class QSpinBox;
class QComboBox;
class QLabel;
class QSplitter;
QT_END_NAMESPACE

// ---------------------------------------------------------------------------
// 消息监控页：左侧主题树 + 右侧实时消息流水
// ---------------------------------------------------------------------------
class MessagesWidget : public QWidget
{
    Q_OBJECT
public:
    explicit MessagesWidget(QWidget *parent = nullptr);

public slots:
    void onMessage(const Mqtt::MessageRecord &rec);
    void onSubscriptionChanged(const QString &clientId);
    void clearAll();

private slots:
    void onFilterChanged();
    void onClearClicked();
    void onPauseToggled(bool paused);
    void onTreeItemSelected();
    void onExportClicked();

private:
    void buildUi();
    bool passFilter(const Mqtt::MessageRecord &rec) const;
    void appendRow(const Mqtt::MessageRecord &rec);
    void refreshTopicTree();
    void trimRows();

    QSplitter   *m_splitter   = nullptr;
    QTreeWidget *m_topicTree  = nullptr;
    QTableWidget *m_table     = nullptr;

    QLineEdit   *m_editFilter = nullptr;
    QComboBox   *m_cmbDir     = nullptr;
    QCheckBox   *m_chkPause   = nullptr;
    QSpinBox    *m_spinMax    = nullptr;
    QPushButton *m_btnClear   = nullptr;
    QPushButton *m_btnExport  = nullptr;
    QLabel      *m_labCount   = nullptr;

    QSet<QString>      m_topics;
    QHash<QString,int> m_topicCounts;
    QHash<QString,int> m_topicSubscribers;

    bool    m_paused = false;
    qint64  m_total  = 0;
    QString m_topicFilter;      // 从主题树选中的完整路径
};

#endif // MESSAGESWIDGET_H

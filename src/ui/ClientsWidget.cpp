#include "ClientsWidget.h"

#include <QTableWidget>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QHeaderView>
#include <QDateTime>
#include <QGroupBox>

ClientsWidget::ClientsWidget(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
}

void ClientsWidget::buildUi()
{
    QVBoxLayout *main = new QVBoxLayout(this);
    main->setContentsMargins(6, 6, 6, 6);
    main->setSpacing(6);

    // ---- 顶部工具条 ----
    QHBoxLayout *bar = new QHBoxLayout();
    m_btnRefresh = new QPushButton(QStringLiteral("刷新列表"), this);
    m_btnKick    = new QPushButton(QStringLiteral("断开选中"), this);
    m_btnKick->setEnabled(false);
    m_labCount   = new QLabel(QStringLiteral("在线：0"), this);
    bar->addWidget(m_btnRefresh);
    bar->addWidget(m_btnKick);
    bar->addStretch();
    bar->addWidget(m_labCount);
    main->addLayout(bar);

    connect(m_btnRefresh, &QPushButton::clicked, this, &ClientsWidget::onRefreshClicked);
    connect(m_btnKick,    &QPushButton::clicked, this, &ClientsWidget::onKickClicked);

    // ---- 主体：上表格 / 下订阅详情 ----
    QSplitter *splitter = new QSplitter(Qt::Vertical, this);

    m_table = new QTableWidget(splitter);
    m_table->setColumnCount(9);
    m_table->setHorizontalHeaderLabels(
        QStringList() << QStringLiteral("ClientID")
                      << QStringLiteral("用户名")
                      << QStringLiteral("来源地址")
                      << QStringLiteral("心跳(s)")
                      << QStringLiteral("持久会话")
                      << QStringLiteral("订阅数")
                      << QStringLiteral("上行/下行")
                      << QStringLiteral("连接时间")
                      << QStringLiteral("最后活跃"));
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setSortingEnabled(false);

    QGroupBox *box = new QGroupBox(QStringLiteral("选中客户端的订阅"), splitter);
    QVBoxLayout *boxLayout = new QVBoxLayout(box);
    m_labDetail  = new QLabel(QStringLiteral("未选中"), box);
    m_subList    = new QListWidget(box);
    boxLayout->addWidget(m_labDetail);
    boxLayout->addWidget(m_subList);

    splitter->addWidget(m_table);
    splitter->addWidget(box);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 1);

    main->addWidget(splitter);

    connect(m_table, &QTableWidget::itemSelectionChanged,
            this, &ClientsWidget::onSelectionChanged);
}

void ClientsWidget::appendRow(int row, const Mqtt::ClientViewInfo &info)
{
    if (m_table->rowCount() <= row)
        m_table->insertRow(row);

    QTableWidgetItem *itemId = new QTableWidgetItem(info.clientId);
    itemId->setData(Qt::UserRole, info.clientId);
    m_table->setItem(row, 0, itemId);
    m_table->setItem(row, 1, new QTableWidgetItem(info.username.isEmpty()
                                                      ? QStringLiteral("-")
                                                      : info.username));
    m_table->setItem(row, 2, new QTableWidgetItem(info.peerAddress));
    m_table->setItem(row, 3, new QTableWidgetItem(QString::number(info.keepAlive)));
    m_table->setItem(row, 4, new QTableWidgetItem(info.cleanSession
                                                      ? QStringLiteral("否")
                                                      : QStringLiteral("是")));
    m_table->setItem(row, 5, new QTableWidgetItem(QString::number(info.subscriptions.size())));
    m_table->setItem(row, 6, new QTableWidgetItem(
                         QStringLiteral("%1 / %2")
                             .arg(info.stats.msgReceived)
                             .arg(info.stats.msgSent)));
    m_table->setItem(row, 7, new QTableWidgetItem(
                         info.connectedAt.toString(QStringLiteral("HH:mm:ss"))));
    m_table->setItem(row, 8, new QTableWidgetItem(
                         info.lastSeen.toString(QStringLiteral("HH:mm:ss"))));

    // 超过 60 秒没有报文 → 标黄提示
    const qint64 idle = info.lastSeen.msecsTo(QDateTime::currentDateTime());
    if (idle > 60000) {
        for (int c = 0; c < m_table->columnCount(); ++c) {
            QTableWidgetItem *it = m_table->item(row, c);
            if (it)
                it->setBackground(QColor(255, 249, 219));
        }
    }
}

void ClientsWidget::onClientsSnapshot(const QList<Mqtt::ClientViewInfo> &list)
{
    m_lastInfo.clear();

    // 保持选中行
    const QString keepId = currentClientId();

    m_table->setRowCount(list.size());
    for (int i = 0; i < list.size(); ++i) {
        const Mqtt::ClientViewInfo &info = list.at(i);
        m_lastInfo.insert(info.clientId, info);
        appendRow(i, info);
    }
    m_table->resizeColumnsToContents();

    m_labCount->setText(QStringLiteral("在线：%1").arg(list.size()));

    // 尝试恢复选中
    if (!keepId.isEmpty()) {
        for (int i = 0; i < m_table->rowCount(); ++i) {
            if (m_table->item(i, 0) && m_table->item(i, 0)->text() == keepId) {
                m_table->selectRow(i);
                break;
            }
        }
    }
    if (m_table->currentRow() < 0)
        m_subList->clear();

    m_btnKick->setEnabled(m_table->currentRow() >= 0);
}

QString ClientsWidget::currentClientId() const
{
    const int row = m_table->currentRow();
    if (row < 0)
        return QString();
    QTableWidgetItem *it = m_table->item(row, 0);
    return it ? it->text() : QString();
}

void ClientsWidget::onSelectionChanged()
{
    m_subList->clear();
    const QString id = currentClientId();
    m_btnKick->setEnabled(!id.isEmpty());
    if (id.isEmpty()) {
        m_labDetail->setText(QStringLiteral("未选中"));
        return;
    }

    const Mqtt::ClientViewInfo info = m_lastInfo.value(id);
    m_labDetail->setText(QStringLiteral("%1   订阅 %2 个主题   收发 %3/%4 字节")
                             .arg(id)
                             .arg(info.subscriptions.size())
                             .arg(info.stats.bytesReceived)
                             .arg(info.stats.bytesSent));

    for (int i = 0; i < info.subscriptions.size(); ++i) {
        const Mqtt::Subscription &s = info.subscriptions.at(i);
        m_subList->addItem(QStringLiteral("%1   [QoS %2]")
                               .arg(s.filter)
                               .arg(Mqtt::qosToString(s.qos)));
    }
}

void ClientsWidget::onKickClicked()
{
    const QString id = currentClientId();
    if (id.isEmpty())
        return;
    emit kickRequested(id);
}

void ClientsWidget::onRefreshClicked()
{
    // Broker 每秒推一次快照，这里立即重绘即可
    m_table->resizeColumnsToContents();
}

void ClientsWidget::clearAll()
{
    m_table->setRowCount(0);
    m_subList->clear();
    m_lastInfo.clear();
    m_labCount->setText(QStringLiteral("在线：0"));
}

#include "MessagesWidget.h"
#include "../mqtt/TopicTree.h"

#include <QSplitter>
#include <QTreeWidget>
#include <QTableWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QSpinBox>
#include <QComboBox>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QDateTime>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QMessageBox>

MessagesWidget::MessagesWidget(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
}

void MessagesWidget::buildUi()
{
    QVBoxLayout *main = new QVBoxLayout(this);
    main->setContentsMargins(6, 6, 6, 6);
    main->setSpacing(6);

    // ---- 顶部过滤条 ----
    QHBoxLayout *bar = new QHBoxLayout();
    bar->addWidget(new QLabel(QStringLiteral("主题过滤："), this));
    m_editFilter = new QLineEdit(this);
    m_editFilter->setPlaceholderText(QStringLiteral("支持子串匹配，如 car/001，留空显示全部"));
    m_editFilter->setClearButtonEnabled(true);
    bar->addWidget(m_editFilter, 1);

    m_cmbDir = new QComboBox(this);
    m_cmbDir->addItem(QStringLiteral("全部方向"));
    m_cmbDir->addItem(QStringLiteral("仅上行 IN"));
    m_cmbDir->addItem(QStringLiteral("仅下行 OUT"));
    bar->addWidget(m_cmbDir);

    m_chkPause = new QCheckBox(QStringLiteral("暂停滚动"), this);
    bar->addWidget(m_chkPause);

    bar->addWidget(new QLabel(QStringLiteral("最大行数："), this));
    m_spinMax = new QSpinBox(this);
    m_spinMax->setRange(100, 100000);
    m_spinMax->setSingleStep(500);
    m_spinMax->setValue(2000);
    bar->addWidget(m_spinMax);

    m_btnExport = new QPushButton(QStringLiteral("导出 CSV"), this);
    m_btnClear  = new QPushButton(QStringLiteral("清空"), this);
    bar->addWidget(m_btnExport);
    bar->addWidget(m_btnClear);

    main->addLayout(bar);

    // ---- 主体：左主题树 / 右消息表 ----
    m_splitter = new QSplitter(Qt::Horizontal, this);

    m_topicTree = new QTreeWidget(m_splitter);
    m_topicTree->setHeaderLabels(QStringList()
                                 << QStringLiteral("主题")
                                 << QStringLiteral("消息数")
                                 << QStringLiteral("订阅者"));
    m_topicTree->setRootIsDecorated(true);
    m_topicTree->setAlternatingRowColors(true);

    m_table = new QTableWidget(m_splitter);
    m_table->setColumnCount(7);
    m_table->setHorizontalHeaderLabels(QStringList()
                                       << QStringLiteral("时间")
                                       << QStringLiteral("方向")
                                       << QStringLiteral("主题")
                                       << QStringLiteral("QoS")
                                       << QStringLiteral("保留")
                                       << QStringLiteral("客户端")
                                       << QStringLiteral("载荷"));
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(true);

    m_splitter->addWidget(m_topicTree);
    m_splitter->addWidget(m_table);
    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 3);

    main->addWidget(m_splitter, 1);

    m_labCount = new QLabel(QStringLiteral("共 0 条"), this);
    main->addWidget(m_labCount);

    connect(m_editFilter, &QLineEdit::textChanged, this, &MessagesWidget::onFilterChanged);
    connect(m_cmbDir, SIGNAL(currentIndexChanged(int)), this, SLOT(onFilterChanged()));
    connect(m_chkPause, &QCheckBox::toggled, this, &MessagesWidget::onPauseToggled);
    connect(m_btnClear, &QPushButton::clicked, this, &MessagesWidget::onClearClicked);
    connect(m_btnExport, &QPushButton::clicked, this, &MessagesWidget::onExportClicked);
    connect(m_topicTree, &QTreeWidget::itemSelectionChanged,
            this, &MessagesWidget::onTreeItemSelected);
}

bool MessagesWidget::passFilter(const Mqtt::MessageRecord &rec) const
{
    const QString keyword = m_editFilter->text().trimmed();
    if (!keyword.isEmpty() && !rec.topic.contains(keyword, Qt::CaseInsensitive))
        return false;

    const int dirIndex = m_cmbDir->currentIndex();
    if (dirIndex == 1 && rec.direction != QLatin1String("IN"))
        return false;
    if (dirIndex == 2 && rec.direction != QLatin1String("OUT"))
        return false;

    if (!m_topicFilter.isEmpty() && rec.topic != m_topicFilter) {
        // 主题树支持按层级过滤：选中 "car" 时包含 "car/001/telemetry"
        if (!rec.topic.startsWith(m_topicFilter + QLatin1Char('/')))
            return false;
    }
    return true;
}

void MessagesWidget::appendRow(const Mqtt::MessageRecord &rec)
{
    const int row = m_table->rowCount();
    m_table->insertRow(row);

    m_table->setItem(row, 0, new QTableWidgetItem(
                         rec.timestamp.toString(QStringLiteral("HH:mm:ss.zzz"))));

    QTableWidgetItem *dirItem = new QTableWidgetItem(rec.direction);
    dirItem->setForeground(rec.direction == QLatin1String("IN")
                               ? QColor(0, 128, 0)
                               : QColor(200, 60, 60));
    m_table->setItem(row, 1, dirItem);

    m_table->setItem(row, 2, new QTableWidgetItem(rec.topic));
    m_table->setItem(row, 3, new QTableWidgetItem(Mqtt::qosToString(rec.qos)));
    m_table->setItem(row, 4, new QTableWidgetItem(rec.retain ? QStringLiteral("是")
                                                             : QStringLiteral("-")));
    m_table->setItem(row, 5, new QTableWidgetItem(rec.clientId));

    // 载荷：控制字符转成可见形式，超长截断
    QString payload = QString::fromUtf8(rec.payload);
    payload.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
    payload.replace(QLatin1Char('\r'), QStringLiteral("\\r"));
    payload.replace(QLatin1Char('\t'), QStringLiteral("\\t"));
    if (payload.length() > 512)
        payload = payload.left(512) + QStringLiteral(" …");
    m_table->setItem(row, 6, new QTableWidgetItem(payload));

    trimRows();
}

void MessagesWidget::trimRows()
{
    const int max = m_spinMax->value();
    while (m_table->rowCount() > max)
        m_table->removeRow(0);
}

void MessagesWidget::onMessage(const Mqtt::MessageRecord &rec)
{
    m_total++;

    m_topics.insert(rec.topic);
    m_topicCounts[rec.topic] = m_topicCounts.value(rec.topic, 0) + 1;

    // 主题树每 50 条刷新一次，避免高频消息拖垮界面
    if (m_total % 50 == 0)
        refreshTopicTree();

    if (m_paused)
        return;
    if (!passFilter(rec))
        return;

    appendRow(rec);
    m_labCount->setText(QStringLiteral("共 %1 条（累计 %2）")
                            .arg(m_table->rowCount()).arg(m_total));
}

// 把 TopicNode 树递归填充进 QTreeWidget
static void fillTopicTree(QTreeWidgetItem *parent, Mqtt::TopicNode *node)
{
    QHashIterator<QString, Mqtt::TopicNode *> it(node->children);
    while (it.hasNext()) {
        it.next();
        Mqtt::TopicNode *child = it.value();
        QTreeWidgetItem *item = new QTreeWidgetItem(parent);
        item->setText(0, child->segment);
        item->setText(1, QString::number(child->messageCount));
        item->setText(2, QString::number(child->subscriberCount));
        item->setData(0, Qt::UserRole, child->fullPath);
        fillTopicTree(item, child);
    }
}

void MessagesWidget::refreshTopicTree()
{
    m_topicTree->clear();

    const QStringList topics = m_topics.toList();
    Mqtt::TopicNode *root = Mqtt::buildTopicTree(topics, m_topicCounts, m_topicSubscribers);

    fillTopicTree(m_topicTree->invisibleRootItem(), root);
    m_topicTree->expandAll();
    m_topicTree->resizeColumnToContents(0);
    delete root;
}

void MessagesWidget::onSubscriptionChanged(const QString &clientId)
{
    Q_UNUSED(clientId)
    // 订阅数变化后重算每层的订阅者（简化：沿用当前统计即可）
}

void MessagesWidget::onTreeItemSelected()
{
    QList<QTreeWidgetItem *> sel = m_topicTree->selectedItems();
    if (sel.isEmpty()) {
        m_topicFilter.clear();
        return;
    }
    m_topicFilter = sel.first()->data(0, Qt::UserRole).toString();
    m_editFilter->setText(m_topicFilter);
}

void MessagesWidget::onFilterChanged()
{
    // 过滤条件变化：用已缓存的记录重放不现实（未保存全量），仅提示用户
    m_labCount->setText(QStringLiteral("共 %1 条（累计 %2）")
                            .arg(m_table->rowCount()).arg(m_total));
}

void MessagesWidget::onPauseToggled(bool paused)
{
    m_paused = paused;
    if (!paused)
        refreshTopicTree();
}

void MessagesWidget::onClearClicked()
{
    m_table->setRowCount(0);
    m_total = 0;
    m_topics.clear();
    m_topicCounts.clear();
    m_topicTree->clear();
    m_labCount->setText(QStringLiteral("共 0 条"));
}

void MessagesWidget::onExportClicked()
{
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("导出消息流水"),
        QStringLiteral("mqtt_messages.csv"),
        QStringLiteral("CSV 文件 (*.csv);;文本文件 (*.txt)"));
    if (path.isEmpty())
        return;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, QStringLiteral("导出失败"),
                             QStringLiteral("无法写入文件：%1").arg(path));
        return;
    }
    QTextStream ts(&f);
    ts.setCodec("UTF-8");
    ts << "时间,方向,主题,QoS,保留,客户端,载荷\n";
    for (int r = 0; r < m_table->rowCount(); ++r) {
        QStringList cols;
        for (int c = 0; c < m_table->columnCount(); ++c) {
            QTableWidgetItem *it = m_table->item(r, c);
            QString v = it ? it->text() : QString();
            v.replace(QLatin1Char('"'), QStringLiteral("\"\""));
            cols << QStringLiteral("\"%1\"").arg(v);
        }
        ts << cols.join(QLatin1Char(',')) << "\n";
    }
    f.close();
    QMessageBox::information(this, QStringLiteral("导出完成"),
                             QStringLiteral("已导出 %1 条到 %2")
                                 .arg(m_table->rowCount()).arg(path));
}

void MessagesWidget::clearAll()
{
    onClearClicked();
}

#include "key_browser.h"
#include "redis_client.h"
#include "resp_codec.h"
#include <QHeaderView>
#include <QMenu>
#include <QInputDialog>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QDebug>
#include <QApplication>
#include <QClipboard>

KeyBrowser::KeyBrowser(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* toolbar = new QHBoxLayout();
    m_filterEdit = new QLineEdit(this);
    m_filterEdit->setPlaceholderText("Filter keys (e.g. user:*)");
    m_refreshBtn = new QPushButton("R", this);
    m_refreshBtn->setToolTip("Refresh key list (F5)");
    m_refreshBtn->setMaximumWidth(40);
    toolbar->addWidget(m_filterEdit);
    toolbar->addWidget(m_refreshBtn);
    layout->addLayout(toolbar);

    m_tree = new QTreeWidget(this);
    m_tree->setHeaderLabels({"Key / Database"});
    m_tree->setRootIsDecorated(true);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->header()->setStretchLastSection(true);
    layout->addWidget(m_tree);

    connect(m_refreshBtn, &QPushButton::clicked, this, &KeyBrowser::refresh);
    connect(m_tree, &QTreeWidget::itemClicked,
            this, &KeyBrowser::onItemClicked);
    connect(m_tree, &QTreeWidget::itemDoubleClicked,
            this, &KeyBrowser::onItemDoubleClicked);
    connect(m_tree, &QTreeWidget::customContextMenuRequested,
            this, &KeyBrowser::onContextMenu);
    connect(m_filterEdit, &QLineEdit::textChanged,
            this, &KeyBrowser::onFilterChanged);
}

void KeyBrowser::setClient(RedisClient* client) {
    m_client = client;
}

void KeyBrowser::refresh() {
    m_tree->clear();
    m_loadedKeys.clear();
    ensureDbRoot(m_currentDb);
    loadKeysForDb(m_currentDb);
}

// ── 使用 KEYS 命令加载，用回调处理响应 ──
void KeyBrowser::loadKeysForDb(int db) {
    if (!m_client || !m_client->isConnected()) return;

    // 先 SELECT 到目标 db
    m_client->sendCommand(
        QStringList() << "SELECT" << QString::number(db),
        nullptr); // 不需要处理 SELECT 响应

    // 发 KEYS *，用回调处理结果
    m_client->sendCommand(
        QStringList() << "KEYS" << "*",
        [this, db](const RespValue& value) {
            if (value.type != RespValue::ARRAY) return;

            QTreeWidgetItem* dbRoot = ensureDbRoot(db);

            for (const auto& item : value.array_val) {
                if (item.type != RespValue::STRING) continue;
                QString key = QString::fromStdString(item.str_val);
                if (m_loadedKeys.contains(key)) continue;
                m_loadedKeys.insert(key);

                auto* treeItem = new QTreeWidgetItem(dbRoot, {key});
                treeItem->setIcon(0, iconForType("unknown"));
                treeItem->setData(0, Qt::UserRole, key);

                // 异步获取类型（用回调更新图标）
                m_client->sendCommand(
                    QStringList() << "TYPE" << key,
                    [this, treeItem](const RespValue& typeVal) {
                        if (typeVal.type == RespValue::STRING) {
                            QString type =
                                QString::fromStdString(typeVal.str_val);
                            treeItem->setIcon(0, iconForType(type));
                            treeItem->setData(0, Qt::UserRole + 1, type);
                        }
                    });
            }
        });
}

// ── Tree 交互 ──

void KeyBrowser::onItemClicked(QTreeWidgetItem* item, int) {
    if (item && item->childCount() == 0 && item->parent()) {
        QString key = item->data(0, Qt::UserRole).toString();
        if (key.isEmpty()) return;
        QString type = item->data(0, Qt::UserRole + 1).toString();
        if (type.isEmpty()) type = "string";
        int db = item->parent()->data(0, Qt::UserRole).toInt();
        emit keySelected(key, db, type);
    }
}

void KeyBrowser::onItemDoubleClicked(QTreeWidgetItem* item, int col) {
    onItemClicked(item, col);
}

void KeyBrowser::onContextMenu(const QPoint& pos) {
    QTreeWidgetItem* item = m_tree->itemAt(pos);
    if (!item || item->childCount() > 0 || !item->parent()) return;

    QString key = item->data(0, Qt::UserRole).toString();
    if (key.isEmpty()) return;

    QMenu menu(this);
    menu.addAction("Open", [this, item]() {
        onItemDoubleClicked(item, 0);
    });
    menu.addAction("Copy Key Name", [key]() {
        QApplication::clipboard()->setText(key);
    });
    menu.addSeparator();
    menu.addAction("Delete", [this, key]() {
        if (QMessageBox::question(this, "Confirm Delete",
                QString("Delete key '%1'?").arg(key))
            == QMessageBox::Yes) {
            m_client->sendCommand(QStringList() << "DEL" << key);
            refresh();
        }
    });
    menu.addAction("Rename...", [this, key]() {
        bool ok;
        QString newName = QInputDialog::getText(this, "Rename Key",
            "New name:", QLineEdit::Normal, key, &ok);
        if (ok && !newName.isEmpty() && newName != key) {
            m_client->sendCommand(
                QStringList() << "RENAME" << key << newName);
            refresh();
        }
    });
    menu.addAction("Set TTL...", [this, key]() {
        bool ok;
        int seconds = QInputDialog::getInt(this, "Set TTL",
            "Expiration (seconds):", 3600, -1, 2147483647, 1, &ok);
        if (ok) {
            m_client->sendCommand(
                QStringList() << "EXPIRE" << key
                              << QString::number(seconds));
        }
    });

    menu.exec(m_tree->viewport()->mapToGlobal(pos));
}

void KeyBrowser::onFilterChanged(const QString& text) {
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* dbRoot = m_tree->topLevelItem(i);
        for (int j = 0; j < dbRoot->childCount(); ++j) {
            QTreeWidgetItem* child = dbRoot->child(j);
            child->setHidden(!child->text(0).contains(
                text, Qt::CaseInsensitive));
        }
    }
}

QTreeWidgetItem* KeyBrowser::ensureDbRoot(int db) {
    QString dbName = QString("DB%1").arg(db);
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        if (m_tree->topLevelItem(i)->text(0) == dbName) {
            return m_tree->topLevelItem(i);
        }
    }
    auto* root = new QTreeWidgetItem(m_tree, {dbName});
    root->setData(0, Qt::UserRole, db);
    root->setExpanded(true);
    return root;
}

QIcon KeyBrowser::iconForType(const QString& type) const {
    QPixmap pix(16, 16);
    QPainter painter(&pix);
    if (type == "string")      painter.fillRect(0,0,16,16, QColor("#4CAF50"));
    else if (type == "list")   painter.fillRect(0,0,16,16, QColor("#2196F3"));
    else if (type == "hash")   painter.fillRect(0,0,16,16, QColor("#FF9800"));
    else if (type == "set")    painter.fillRect(0,0,16,16, QColor("#9C27B0"));
    else if (type == "zset")   painter.fillRect(0,0,16,16, QColor("#F44336"));
    else                       painter.fillRect(0,0,16,16, QColor("#9E9E9E"));
    painter.end();
    return QIcon(pix);
}

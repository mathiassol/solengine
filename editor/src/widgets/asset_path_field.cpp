#include "widgets/asset_path_field.h"

#include <QFileDialog>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>

AssetPathField::AssetPathField(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    m_lineEdit = new QLineEdit(this);
    m_lineEdit->setMinimumHeight(28);

    auto* browseButton = new QPushButton("...", this);
    browseButton->setFixedSize(32, 28);

    layout->addWidget(m_lineEdit, 1);
    layout->addWidget(browseButton);

    connect(m_lineEdit, &QLineEdit::editingFinished, this, &AssetPathField::commit);
    connect(browseButton, &QPushButton::clicked, this, &AssetPathField::browse);
}

QString AssetPathField::path() const
{
    return m_lineEdit->text();
}

void AssetPathField::setPath(const QString& path)
{
    if (m_lineEdit->text() == path)
        return;
    m_lineEdit->setText(path);
    emit pathChanged(path);
}

void AssetPathField::setPathSilent(const QString& path)
{
    QSignalBlocker blocker(m_lineEdit);
    m_lineEdit->setText(path);
}

void AssetPathField::browse()
{
    const QString chosen = QFileDialog::getOpenFileName(this, tr("Select Asset"), m_lineEdit->text());
    if (!chosen.isEmpty())
        setPath(chosen);
}

void AssetPathField::commit()
{
    emit pathChanged(m_lineEdit->text());
}

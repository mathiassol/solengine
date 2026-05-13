#pragma once

#include <QWidget>

class QLineEdit;

class AssetPathField : public QWidget
{
    Q_OBJECT

public:
    explicit AssetPathField(QWidget* parent = nullptr);

    QString path() const;
    void setPath(const QString& path);
    void setPathSilent(const QString& path);

signals:
    void pathChanged(const QString& path);

private:
    void browse();
    void commit();

    QLineEdit* m_lineEdit{};
};

#pragma once

#include <QWidget>
#include <glm/glm.hpp>

class NumberField;

class Vec3Field : public QWidget
{
    Q_OBJECT

public:
    explicit Vec3Field(QWidget* parent = nullptr);

    void setValue(const glm::vec3& v);
    glm::vec3 value() const;

signals:
    void valueChanged(const glm::vec3& v);

private:
    NumberField* m_x{};
    NumberField* m_y{};
    NumberField* m_z{};
};

class Vec4Field : public QWidget
{
    Q_OBJECT

public:
    explicit Vec4Field(QWidget* parent = nullptr);

    void setValue(const glm::vec4& v);
    glm::vec4 value() const;

signals:
    void valueChanged(const glm::vec4& v);

private:
    NumberField* m_x{};
    NumberField* m_y{};
    NumberField* m_z{};
    NumberField* m_w{};
};

#include "project_launcher.h"

#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFrame>
#include <QFont>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QSettings>
#include <QDir>
#include <QFileInfo>
#include <QStringList>
#include <QApplication>
#include <QStyle>
#include <QPixmap>
#include <fstream>

// ---------------------------------------------------------------------------
static bool createProjectFiles(const QString& dir, const QString& name)
{
    QDir d(dir);
    if (!d.mkpath(".") || !d.mkpath("scenes"))
        return false;

    // project.sol
    {
        std::ofstream f((dir + "/project.sol").toStdString());
        if (!f) return false;
        f << "{\n"
          << "  \"sol_project\": 1,\n"
          << "  \"name\": \"" << name.toStdString() << "\",\n"
          << "  \"version\": \"0.1.0\",\n"
          << "  \"main_scene\": \"scenes/main.solscene\",\n"
          << "  \"window\": {\n"
          << "    \"title\": \"" << name.toStdString() << "\",\n"
          << "    \"width\": 1280,\n"
          << "    \"height\": 720,\n"
          << "    \"vsync\": true\n"
          << "  }\n"
          << "}\n";
    }

    // scenes/main.solscene
    {
        std::ofstream f((dir + "/scenes/main.solscene").toStdString());
        if (!f) return false;
        f << "{\n"
          << "  \"name\": \"Main\",\n"
          << "  \"root\": {\n"
          << "    \"type\": \"Node3D\",\n"
          << "    \"name\": \"Root\",\n"
          << "    \"children\": [\n"
          << "      {\n"
          << "        \"type\": \"DirectionalLight\",\n"
          << "        \"name\": \"Sun\",\n"
          << "        \"rotation\": [-45.0, 30.0, 0.0],\n"
          << "        \"intensity\": 3.0,\n"
          << "        \"cast_shadow\": true,\n"
          << "        \"shadow_mode\": 2\n"
          << "      },\n"
          << "      {\n"
          << "        \"type\": \"Camera3D\",\n"
          << "        \"name\": \"Camera\",\n"
          << "        \"position\": [0.0, 1.0, 5.0],\n"
          << "        \"current\": true\n"
          << "      }\n"
          << "    ]\n"
          << "  }\n"
          << "}\n";
    }

    return true;
}

// ---------------------------------------------------------------------------
ProjectLauncherDialog::ProjectLauncherDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("SolEngine — Project Launcher");
    setMinimumSize(720, 480);
    resize(800, 520);
    buildUi();
    loadRecent();
}

// ---------------------------------------------------------------------------
void ProjectLauncherDialog::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── Header bar ─────────────────────────────────────────────────────────
    auto* header = new QFrame(this);
    header->setObjectName("LauncherHeader");
    header->setFixedHeight(72);
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(24, 0, 24, 0);

    auto* titleLabel = new QLabel("SolEngine", header);
    {
        QFont f = titleLabel->font();
        f.setPointSize(22);
        f.setBold(true);
        titleLabel->setFont(f);
        titleLabel->setObjectName("LauncherTitle");
    }

    auto* subLabel = new QLabel("Project Launcher", header);
    subLabel->setObjectName("LauncherSub");
    {
        QFont f = subLabel->font();
        f.setPointSize(11);
        subLabel->setFont(f);
    }

    headerLayout->addWidget(titleLabel);
    headerLayout->addSpacing(12);
    headerLayout->addWidget(subLabel);
    headerLayout->addStretch();

    root->addWidget(header);

    // ── Separator ──────────────────────────────────────────────────────────
    auto* sep = new QFrame(this);
    sep->setFrameShape(QFrame::HLine);
    sep->setObjectName("LauncherSep");
    root->addWidget(sep);

    // ── Body ───────────────────────────────────────────────────────────────
    auto* body = new QWidget(this);
    auto* bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(16, 12, 16, 12);
    bodyLayout->setSpacing(12);

    // Left: recent list
    auto* listBox = new QVBoxLayout();
    auto* recentLabel = new QLabel("Recent Projects", body);
    {
        QFont f = recentLabel->font();
        f.setBold(true);
        recentLabel->setFont(f);
    }
    listBox->addWidget(recentLabel);

    m_list = new QListWidget(body);
    m_list->setAlternatingRowColors(true);
    m_list->setSortingEnabled(false);
    connect(m_list, &QListWidget::itemDoubleClicked,
            this,   &ProjectLauncherDialog::onItemDoubleClicked);
    connect(m_list, &QListWidget::itemSelectionChanged,
            this,   &ProjectLauncherDialog::onSelectionChanged);
    listBox->addWidget(m_list);

    bodyLayout->addLayout(listBox, 1);

    // Right: action buttons
    auto* btnBox = new QVBoxLayout();
    btnBox->setSpacing(8);
    btnBox->setAlignment(Qt::AlignTop);

    auto makeBtn = [&](const QString& text) {
        auto* b = new QPushButton(text, body);
        b->setMinimumWidth(140);
        b->setFixedHeight(36);
        btnBox->addWidget(b);
        return b;
    };

    auto* newBtn    = makeBtn("New Project...");
    auto* openBtn   = makeBtn("Open Folder...");
    m_openBtn       = makeBtn("Open");
    btnBox->addSpacing(8);
    m_removeBtn     = makeBtn("Remove from List");

    m_openBtn->setEnabled(false);
    m_removeBtn->setEnabled(false);
    m_openBtn->setDefault(true);

    connect(newBtn,       &QPushButton::clicked, this, &ProjectLauncherDialog::onNewProject);
    connect(openBtn,      &QPushButton::clicked, this, &ProjectLauncherDialog::onOpenProject);
    connect(m_openBtn,    &QPushButton::clicked, this, &ProjectLauncherDialog::onOpenSelected);
    connect(m_removeBtn,  &QPushButton::clicked, this, &ProjectLauncherDialog::onRemoveSelected);

    bodyLayout->addLayout(btnBox);

    root->addWidget(body, 1);

    // ── Bottom bar ─────────────────────────────────────────────────────────
    auto* bottomSep = new QFrame(this);
    bottomSep->setFrameShape(QFrame::HLine);
    bottomSep->setObjectName("LauncherSep");
    root->addWidget(bottomSep);

    auto* bottomBar = new QWidget(this);
    auto* bottomLayout = new QHBoxLayout(bottomBar);
    bottomLayout->setContentsMargins(16, 8, 16, 8);
    bottomLayout->addStretch();
    auto* cancelBtn = new QPushButton("Cancel", bottomBar);
    cancelBtn->setFixedHeight(32);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    bottomLayout->addWidget(cancelBtn);
    root->addWidget(bottomBar);
}

// ---------------------------------------------------------------------------
void ProjectLauncherDialog::loadRecent()
{
    QSettings s("SolEngine", "Launcher");
    QStringList paths = s.value("recentProjects").toStringList();

    m_list->clear();
    for (const QString& path : paths) {
        QFileInfo fi(path);
        auto* item = new QListWidgetItem(m_list);
        item->setData(Qt::UserRole, path);
        // Show project name (dir name) + path below
        const QString name = fi.fileName().isEmpty() ? path : fi.fileName();
        item->setText(name + "\n" + path);
        item->setToolTip(path);
        // Dim entries for missing directories
        if (!fi.isDir())
            item->setForeground(Qt::gray);
    }
}

void ProjectLauncherDialog::saveRecent()
{
    QStringList paths;
    for (int i = 0; i < m_list->count(); ++i)
        paths << m_list->item(i)->data(Qt::UserRole).toString();

    QSettings s("SolEngine", "Launcher");
    s.setValue("recentProjects", paths);
}

void ProjectLauncherDialog::addRecent(const QString& path)
{
    QSettings s("SolEngine", "Launcher");
    QStringList paths = s.value("recentProjects").toStringList();
    paths.removeAll(path);           // avoid duplicates
    paths.prepend(path);             // most-recent-first
    while (paths.size() > 20)
        paths.removeLast();
    s.setValue("recentProjects", paths);
    loadRecent();
}

// ---------------------------------------------------------------------------
void ProjectLauncherDialog::openProject(const QString& path)
{
    QFileInfo fi(path + "/project.sol");
    if (!fi.exists()) {
        QMessageBox::warning(this, "Not a SolEngine Project",
            "No project.sol found in:\n" + path +
            "\n\nPlease select a valid SolEngine project directory.");
        return;
    }

    m_selected = path;
    addRecent(path);
    emit projectSelected(path);
    accept();
}

// ---------------------------------------------------------------------------
void ProjectLauncherDialog::onNewProject()
{
    // Ask for project name
    bool ok = false;
    QString name = QInputDialog::getText(this, "New Project",
        "Project name:", QLineEdit::Normal, "MyGame", &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    name = name.trimmed();

    // Ask for parent directory
    QString parentDir = QFileDialog::getExistingDirectory(this,
        "Choose location for new project", QDir::homePath());
    if (parentDir.isEmpty()) return;

    QString projectDir = parentDir + "/" + name;
    if (QDir(projectDir).exists()) {
        QMessageBox::warning(this, "Already Exists",
            "A directory named '" + name + "' already exists at:\n" + parentDir);
        return;
    }

    if (!createProjectFiles(projectDir, name)) {
        QMessageBox::critical(this, "Error",
            "Failed to create project files in:\n" + projectDir);
        return;
    }

    openProject(projectDir);
}

void ProjectLauncherDialog::onOpenProject()
{
    QString dir = QFileDialog::getExistingDirectory(this,
        "Open SolEngine Project", QDir::homePath());
    if (dir.isEmpty()) return;
    openProject(dir);
}

void ProjectLauncherDialog::onOpenSelected()
{
    auto* item = m_list->currentItem();
    if (!item) return;
    openProject(item->data(Qt::UserRole).toString());
}

void ProjectLauncherDialog::onRemoveSelected()
{
    auto* item = m_list->currentItem();
    if (!item) return;
    delete item;
    saveRecent();
    onSelectionChanged();
}

void ProjectLauncherDialog::onItemDoubleClicked(QListWidgetItem* item)
{
    if (!item) return;
    openProject(item->data(Qt::UserRole).toString());
}

void ProjectLauncherDialog::onSelectionChanged()
{
    const bool has = m_list->currentItem() != nullptr;
    m_openBtn->setEnabled(has);
    m_removeBtn->setEnabled(has);
}

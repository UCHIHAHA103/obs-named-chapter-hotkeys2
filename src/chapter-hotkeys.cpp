#include "chapter-hotkeys.hpp"
#include "plugin-logger.h"

#include <functional>
#include <QCoreApplication>
#include <QAction>
#include <QKeyEvent>
#include <QMainWindow>
#include <QObject>
#include <QMenu>
#include <QUuid>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScreen>
#include <QRect>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDir>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QColor>
#include <QPixmap>
#include <QPainter>
#include <QMap>
#include <QSet>
#include <QMessageBox>
#include <QTimer>
#include <QTextEdit>
#include <QFileDialog>
#include <QInputDialog>
#include <QClipboard>
#include <QApplication>
#include <QDateTime>
#include <QGroupBox>
#include <QMutexLocker>
#include <QScrollBar>
#include <QStyle>

#ifdef _WIN32
#include <windows.h>
#endif

using namespace std;

// ============================================================================
// 静态变量
// ============================================================================
std::atomic<bool> ChapterWithCommentDialog::s_isDialogOpen(false);
QMutex ChapterWithCommentDialog::s_dialogMutex;

static QMap<QString, QString> createColorMap()
{
	QMap<QString, QString> map;
	map["#718637"] = "green";
	map["#D22C36"] = "red";
	map["#AF8BB1"] = "purple";
	map["#E96F24"] = "orange";
	map["#D0A12B"] = "yellow";
	map["#FFFFFF"] = "white";
	map["#428DFC"] = "blue";
	map["#19F4D6"] = "cyan";
	map["green"] = "green";
	map["red"] = "red";
	map["purple"] = "purple";
	map["orange"] = "orange";
	map["yellow"] = "yellow";
	map["white"] = "white";
	map["blue"] = "blue";
	map["cyan"] = "cyan";
	return map;
}

static QString getColorName(const QString &colorHex)
{
	if (colorHex == "#D22C36") return "red";
	if (colorHex == "#AF8BB1") return "purple";
	if (colorHex == "#E96F24") return "orange";
	if (colorHex == "#D0A12B") return "yellow";
	if (colorHex == "#FFFFFF") return "white";
	if (colorHex == "#428DFC") return "blue";
	if (colorHex == "#19F4D6") return "cyan";
	if (colorHex == "red") return "red";
	if (colorHex == "purple") return "purple";
	if (colorHex == "orange") return "orange";
	if (colorHex == "yellow") return "yellow";
	if (colorHex == "white") return "white";
	if (colorHex == "blue") return "blue";
	if (colorHex == "cyan") return "cyan";
	return "green";
}

static QString getColorHex(const QString &colorNameOrHex)
{
	static QMap<QString, QString> colorMap = createColorMap();
	QString lower = colorNameOrHex.toLower();
	if (lower.startsWith("#")) {
		return lower;
	}
	return colorMap.value(lower, "#718637");
}

static QColor getColorFromHex(const QString &colorNameOrHex)
{
	return QColor(getColorHex(colorNameOrHex));
}

ChapterHotkeyUI *hk_edit;
MarkerLivePanel *g_livePanel = nullptr;
bool g_enableComments = false;

// 对话框重入保护：使用原子标志防止热键线程并发触发
static std::atomic<bool> g_showDialogPending(false);

// ============================================================================
// ChapterHotkeyUI 构造函数 - 增加配置方案UI和导入导出按钮
// ============================================================================
ChapterHotkeyUI::ChapterHotkeyUI(QWidget *parent)
	: QDialog(parent),
	  ui(new Ui_HotkeyChaptersDialog)
{
	ui->setupUi(this);
	setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
	setMinimumWidth(340);
	resize(340, 520);
	ui->listWidget->setSortingEnabled(true);

	QVBoxLayout *mainLayout = qobject_cast<QVBoxLayout *>(layout());
	if (mainLayout) {
		mainLayout->setSpacing(2);
		mainLayout->setContentsMargins(8, 8, 8, 8);
		
		// === 方案选择行（紧凑单行，无GroupBox） ===
		QHBoxLayout *profileLayout = new QHBoxLayout;
		profileLayout->setSpacing(4);
		profileLayout->setContentsMargins(0, 0, 0, 0);
		
		profileCombo = new QComboBox(this);
		profileCombo->setMinimumWidth(120);
		profileCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		profileLayout->addWidget(profileCombo);
		
		saveProfileBtn = new QPushButton(this);
		saveProfileBtn->setToolTip("新建方案");
		saveProfileBtn->setFixedSize(26, 26);
		saveProfileBtn->setIcon(QIcon(":/res/images/plus.svg"));
		saveProfileBtn->setIconSize(QSize(14, 14));
		profileLayout->addWidget(saveProfileBtn);
		
		deleteProfileBtn = new QPushButton(this);
		deleteProfileBtn->setToolTip("删除选中方案");
		deleteProfileBtn->setFixedSize(26, 26);
		deleteProfileBtn->setIcon(QIcon(":/res/images/minus.svg"));
		deleteProfileBtn->setIconSize(QSize(14, 14));
		profileLayout->addWidget(deleteProfileBtn);
		
		renameProfileBtn = new QPushButton(this);
		renameProfileBtn->setToolTip("重命名方案");
		renameProfileBtn->setFixedSize(26, 26);
		renameProfileBtn->setIcon(QIcon(":/res/images/sources/text.svg"));
		renameProfileBtn->setIconSize(QSize(14, 14));
		profileLayout->addWidget(renameProfileBtn);
		
		mainLayout->insertLayout(0, profileLayout);
		
		// === 标记列表（由 .ui 定义，已在 mainLayout 中） ===
		// listWidget 在 .ui 中是 index 0，insertLayout(0) 后变成 index 1
		
		// === 工具栏 + 颜色按钮合并行 ===
		// .ui 定义了 toolbar(index 2) 和 colorButtonsWidget(index 3)
		// 隐藏原有的 toolbar 和颜色按钮区域，用新的合并行替换
		ui->toolbar->setVisible(false);
		ui->colorButtonsWidget->setVisible(false);
		
		QHBoxLayout *toolColorLayout = new QHBoxLayout;
		toolColorLayout->setSpacing(2);
		toolColorLayout->setContentsMargins(0, 0, 0, 0);
		
		// 工具按钮：添加、删除、重命名、刷新
		QPushButton *addBtn = new QPushButton(this);
		addBtn->setFixedSize(26, 26);
		addBtn->setIcon(QIcon(":/res/images/plus.svg"));
		addBtn->setIconSize(QSize(14, 14));
		addBtn->setToolTip("添加标记");
		connect(addBtn, &QPushButton::clicked, this, &ChapterHotkeyUI::on_actionAddHotkey_triggered);
		toolColorLayout->addWidget(addBtn);
		
		QPushButton *removeBtn = new QPushButton(this);
		removeBtn->setFixedSize(26, 26);
		removeBtn->setIcon(QIcon(":/res/images/minus.svg"));
		removeBtn->setIconSize(QSize(14, 14));
		removeBtn->setToolTip("删除标记");
		connect(removeBtn, &QPushButton::clicked, this, &ChapterHotkeyUI::on_actionRemoveHotkey_triggered);
		toolColorLayout->addWidget(removeBtn);
		
		QPushButton *renameBtn = new QPushButton(this);
		renameBtn->setFixedSize(26, 26);
		renameBtn->setIcon(QIcon(":/res/images/sources/text.svg"));
		renameBtn->setIconSize(QSize(14, 14));
		renameBtn->setToolTip("重命名标记");
		connect(renameBtn, &QPushButton::clicked, this, &ChapterHotkeyUI::on_actionRenameHotkey_triggered);
		toolColorLayout->addWidget(renameBtn);
		
		refreshHotkeysBtn = new QPushButton(this);
		refreshHotkeysBtn->setFixedSize(26, 26);
		refreshHotkeysBtn->setIcon(QIcon(":/res/images/refresh.svg"));
		refreshHotkeysBtn->setIconSize(QSize(14, 14));
		refreshHotkeysBtn->setToolTip("刷新快捷键显示");
		toolColorLayout->addWidget(refreshHotkeysBtn);
		
		// 分隔线
		QFrame *sep = new QFrame(this);
		sep->setFrameShape(QFrame::VLine);
		sep->setFrameShadow(QFrame::Sunken);
		sep->setFixedHeight(20);
		toolColorLayout->addWidget(sep);
		
		// 颜色按钮（正圆 22x22）
		const int colorBtnSize = 22;
		const int colorBtnRadius = colorBtnSize / 2;
		auto makeColorBtn = [this, toolColorLayout, colorBtnSize, colorBtnRadius](std::function<void()> callback, const QString &color) {
			QPushButton *btn = new QPushButton(this);
			btn->setFixedSize(colorBtnSize, colorBtnSize);
			btn->setMinimumSize(colorBtnSize, colorBtnSize);
			btn->setMaximumSize(colorBtnSize, colorBtnSize);
			btn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
			btn->setStyleSheet(
				QString("QPushButton { background-color: %1; border-radius: %3px; border: 1px solid #333; min-width: %4px; min-height: %4px; max-width: %4px; max-height: %4px; padding: 0px; } "
					"QPushButton:hover { border: 2px solid %2; }")
				.arg(color)
				.arg(color == "#FFFFFF" || color == "#D0A12B" ? "black" : "white")
				.arg(colorBtnRadius)
				.arg(colorBtnSize));
			connect(btn, &QPushButton::clicked, this, callback);
			toolColorLayout->addWidget(btn);
		};
		makeColorBtn([this]() { on_colorButtonGreen_clicked(); }, "#718637");
		makeColorBtn([this]() { on_colorButtonRed_clicked(); }, "#D22C36");
		makeColorBtn([this]() { on_colorButtonPurple_clicked(); }, "#AF8BB1");
		makeColorBtn([this]() { on_colorButtonOrange_clicked(); }, "#E96F24");
		makeColorBtn([this]() { on_colorButtonYellow_clicked(); }, "#D0A12B");
		makeColorBtn([this]() { on_colorButtonWhite_clicked(); }, "#FFFFFF");
		makeColorBtn([this]() { on_colorButtonBlue_clicked(); }, "#428DFC");
		makeColorBtn([this]() { on_colorButtonCyan_clicked(); }, "#19F4D6");
		
		toolColorLayout->addStretch();
		
		// 插入到 listWidget 之后（隐藏的toolbar/colorButtons之前的位置）
		// mainLayout: [0]profileLayout [1]listWidget [2]toolbar(hidden) [3]colors(hidden) [4]okLayout
		mainLayout->insertLayout(2, toolColorLayout);
		
		// === 分隔线（工具栏与底部栏之间，紧凑无多余间距） ===
		QFrame *hLine = new QFrame(this);
		hLine->setFrameShape(QFrame::HLine);
		hLine->setFrameShadow(QFrame::Plain);
		hLine->setFixedHeight(1);
		hLine->setStyleSheet("QFrame { background: #444; border: none; margin: 0; padding: 0; }");
		hLine->setContentsMargins(0, 0, 0, 0);
		mainLayout->addWidget(hLine);
		
		// === 底部栏：注释开关 + 导入导出 + 确定 ===
		// 隐藏原有的 OK 按钮行
		ui->accept->setVisible(false);
		
		QHBoxLayout *bottomLayout = new QHBoxLayout;
		bottomLayout->setSpacing(6);
		bottomLayout->setContentsMargins(0, 0, 0, 0);
		
		// 注释复选框
		enableCommentsCheckBox = new QCheckBox("注释", this);
		enableCommentsCheckBox->setChecked(false);
		connect(enableCommentsCheckBox, &QCheckBox::toggled, [](bool checked) {
			g_enableComments = checked;
		});
		bottomLayout->addWidget(enableCommentsCheckBox);
		
		bottomLayout->addStretch();
		
		// 导出按钮（使用OBS主题SVG图标，风格一致）
		exportBtn = new QPushButton(this);
		exportBtn->setToolTip("导出配置");
		exportBtn->setFixedSize(26, 26);
		exportBtn->setIcon(QIcon::fromTheme("document-save-as",
			QIcon(":/res/images/save.svg")));
		exportBtn->setIconSize(QSize(14, 14));
		bottomLayout->addWidget(exportBtn);
		
		// 导入按钮（使用OBS主题SVG图标，风格一致）
		importBtn = new QPushButton(this);
		importBtn->setToolTip("导入配置");
		importBtn->setFixedSize(26, 26);
		importBtn->setIcon(QIcon::fromTheme("document-open",
			QIcon(":/res/images/revert.svg")));
		importBtn->setIconSize(QSize(14, 14));
		bottomLayout->addWidget(importBtn);
		
		// 确定按钮（加宽确保文字显示完整）
		QPushButton *okBtn = new QPushButton("确定", this);
		okBtn->setFixedWidth(60);
		connect(okBtn, &QPushButton::clicked, this, &QDialog::accept);
		bottomLayout->addWidget(okBtn);
		
		mainLayout->addLayout(bottomLayout);
		
		// 列表项分隔线由 HotkeyItemDelegate::paint 绘制，不使用 stylesheet
		// （stylesheet 会覆盖 Qt 默认的选中/hover 高亮样式）
		
		// === 连接信号 ===
		connect(profileCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
			this, &ChapterHotkeyUI::onProfileComboChanged);
		connect(saveProfileBtn, &QPushButton::clicked, this, &ChapterHotkeyUI::onSaveProfileClicked);
		connect(deleteProfileBtn, &QPushButton::clicked, this, &ChapterHotkeyUI::onDeleteProfileClicked);
		connect(renameProfileBtn, &QPushButton::clicked, this, &ChapterHotkeyUI::onRenameProfileClicked);
		connect(refreshHotkeysBtn, &QPushButton::clicked, this, &ChapterHotkeyUI::onRefreshHotkeysClicked);
		connect(exportBtn, &QPushButton::clicked, this, &ChapterHotkeyUI::onExportClicked);
		connect(importBtn, &QPushButton::clicked, this, &ChapterHotkeyUI::onImportClicked);
		
		// 标记列表变更时自动保存（覆盖重命名等场景）
		connect(ui->listWidget, &QListWidget::itemChanged, this, [this](QListWidgetItem *) {
			autoSaveCurrentProfile();
		});
		
		// 设置自定义绘制代理：标记名称左对齐、快捷键右对齐灰色显示
		ui->listWidget->setItemDelegate(new HotkeyItemDelegate(ui->listWidget));
		
		// 初始化方案下拉框
		refreshProfileCombo();
	}
	
	plog(LOG_INFO, "ChapterHotkeyUI initialized, window size: %dx%d", width(), height());
}

void ChapterHotkeyUI::ShowHideDialog()
{
	if (!isVisible()) {
		setVisible(true);
		// 每次打开对话框时刷新快捷键显示，确保显示最新的绑定状态
		refreshAllHotkeyDisplays();
		plog(LOG_INFO, "Dialog opened, hotkey displays refreshed");
	} else {
		close();
	}
}

bool ChapterHotkeyUI::IsCommentsEnabled()
{
	return g_enableComments;
}

QStringList ChapterHotkeyUI::GetAllChapterNames()
{
	QStringList names;
	if (hk_edit) {
		for (int i = 0; i < hk_edit->ui->listWidget->count(); i++) {
			auto item = hk_edit->ui->listWidget->item(i);
			QString name = item->data(Name).toString();
			if (!name.isEmpty()) {
				names.append(name);
			}
		}
	}
	return names;
}

// ============================================================================
// 标记快捷键操作
// ============================================================================
void ChapterHotkeyUI::on_actionAddHotkey_triggered()
{
	string name;
	bool accepted = ChapterNameDialog::AskForName(
		this, "添加标记名称", "请输入标记名称", name);
	if (!accepted || name.empty())
		return;

	auto uuid = QUuid::createUuid();
	QString id = "chapter_hotkey_" + uuid.toString(QUuid::WithoutBraces);

	auto item = new ChapterHotkeyItem(id, name.c_str(), nullptr, "#718637");
	ui->listWidget->addItem(item);
	ui->listWidget->sortItems();
	
	autoSaveCurrentProfile();
}

void ChapterHotkeyUI::on_actionRemoveHotkey_triggered()
{
	auto item = ui->listWidget->currentItem();
	if (item) {
		plog(LOG_INFO, "User action: Remove hotkey '%s'", qUtf8Printable(item->data(Name).toString()));
	}
	delete item;
	ui->listWidget->sortItems();
	
	autoSaveCurrentProfile();
}

void ChapterHotkeyUI::on_actionRenameHotkey_triggered()
{
	auto item = ui->listWidget->currentItem();
	if (!item) return;
	
	Qt::ItemFlags flags = item->flags();
	item->setFlags(flags | Qt::ItemIsEditable);
	ui->listWidget->editItem(item);
	item->setFlags(flags);
}

void ChapterHotkeyUI::on_colorButtonGreen_clicked() { setSelectedItemColor("#718637"); }
void ChapterHotkeyUI::on_colorButtonRed_clicked() { setSelectedItemColor("#D22C36"); }
void ChapterHotkeyUI::on_colorButtonPurple_clicked() { setSelectedItemColor("#AF8BB1"); }
void ChapterHotkeyUI::on_colorButtonOrange_clicked() { setSelectedItemColor("#E96F24"); }
void ChapterHotkeyUI::on_colorButtonYellow_clicked() { setSelectedItemColor("#D0A12B"); }
void ChapterHotkeyUI::on_colorButtonWhite_clicked() { setSelectedItemColor("#FFFFFF"); }
void ChapterHotkeyUI::on_colorButtonBlue_clicked() { setSelectedItemColor("#428DFC"); }
void ChapterHotkeyUI::on_colorButtonCyan_clicked() { setSelectedItemColor("#19F4D6"); }

void ChapterHotkeyUI::setSelectedItemColor(const QString &color)
{
	auto item = ui->listWidget->currentItem();
	if (item) {
		ChapterHotkeyItem *hkItem = dynamic_cast<ChapterHotkeyItem*>(item);
		if (hkItem) {
			hkItem->setColor(color);
			hkItem->updateDisplayText();
		}
		
		autoSaveCurrentProfile();
	}
}

// ============================================================================
// 外部配置文件路径（已统一到 profiles 目录）
// ============================================================================
QString ChapterHotkeyUI::getExternalConfigPath()
{
	// 统一使用 profiles 目录，不再使用独立的 chapter-markers-config.json
	// 返回当前活跃方案的文件路径
	QString profileName = currentProfileName.isEmpty() ? QString::fromUtf8("预设") : currentProfileName;
	QString configPath = getProfilesDir() + "/" + profileName + ".json";
	return configPath;
}

// ============================================================================
// 迁移旧版 chapter-markers-config.json 到 profiles/预设.json
// ============================================================================
void ChapterHotkeyUI::migrateOldConfig()
{
	QString oldConfigPath = "C:\\Program Files (x86)\\Common Files\\Adobe\\CEP\\extensions\\VideoMarkerExtractor\\VideoMarkerExtractor_Data\\chapter-markers-config.json";
	QString newConfigPath = getProfilesDir() + "/" + QString::fromUtf8("预设") + ".json";
	
	// 如果旧配置文件存在且新预设文件不存在，执行迁移
	if (QFile::exists(oldConfigPath) && !QFile::exists(newConfigPath)) {
		QFile oldFile(oldConfigPath);
		if (oldFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
			QByteArray data = oldFile.readAll();
			oldFile.close();
			
			// 解析并升级格式
			QJsonDocument doc = QJsonDocument::fromJson(data);
			if (doc.isObject()) {
				QJsonObject obj = doc.object();
				obj["name"] = QString::fromUtf8("预设");
				obj["version"] = "3.0";
				if (!obj.contains("createdAt")) {
					obj["createdAt"] = QDateTime::currentDateTime().toString(Qt::ISODate);
				}
				
				// 写入新位置
				QFile newFile(newConfigPath);
				if (newFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
					newFile.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
					newFile.close();
					plog(LOG_INFO, "Migrated old config to profiles/预设.json");
				}
			}
			
			// 删除旧文件
			QFile::remove(oldConfigPath);
			plog(LOG_INFO, "Removed old chapter-markers-config.json");
		}
	}
	
	// 确保预设文件始终存在（首次安装时创建空预设）
	if (!QFile::exists(newConfigPath)) {
		QJsonObject presetObj;
		presetObj["name"] = QString::fromUtf8("预设");
		presetObj["version"] = "3.0";
		presetObj["enableComments"] = false;
		presetObj["createdAt"] = QDateTime::currentDateTime().toString(Qt::ISODate);
		presetObj["markers"] = QJsonArray();
		
		QFile newFile(newConfigPath);
		if (newFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
			newFile.write(QJsonDocument(presetObj).toJson(QJsonDocument::Indented));
			newFile.close();
			plog(LOG_INFO, "Created default preset file: profiles/预设.json");
		}
	}
}

// ============================================================================
// 配置方案目录
// ============================================================================
QString ChapterHotkeyUI::getProfilesDir()
{
	QString cepPath = "C:\\Program Files (x86)\\Common Files\\Adobe\\CEP\\extensions\\VideoMarkerExtractor\\VideoMarkerExtractor_Data\\profiles";
	QDir dir(cepPath);
	if (!dir.exists()) {
		dir.mkpath(".");
	}
	return cepPath;
}

// ============================================================================
// 保存到外部配置文件
// ============================================================================
void ChapterHotkeyUI::saveToExternalConfig()
{
	// 配置已统一到 profiles 目录，直接保存到当前方案文件
	if (currentProfileName.isEmpty()) {
		currentProfileName = QString::fromUtf8("预设");
	}
	saveCurrentAsProfile(currentProfileName);
}

void ChapterHotkeyUI::loadFromExternalConfig()
{
	// 执行旧版配置迁移
	migrateOldConfig();
	
	// 默认加载"预设"方案
	currentProfileName = QString::fromUtf8("预设");
	QString presetPath = getProfilesDir() + "/" + currentProfileName + ".json";
	
	if (QFile::exists(presetPath)) {
		loadProfile(currentProfileName);
	}
	
	refreshProfileCombo();
}

// ============================================================================
// 配置方案管理
// ============================================================================
void ChapterHotkeyUI::saveCurrentAsProfile(const QString &profileName, const QString &description)
{
	QJsonArray markersArray;
	for (int i = 0; i < ui->listWidget->count(); i++) {
		auto item = ui->listWidget->item(i);
		QJsonObject markerObj;
		markerObj["name"] = item->data(Name).toString();
		markerObj["uuid"] = item->data(HotkeyId).toString();
		markerObj["color"] = item->data(Color).toString();
		
		// 保存完整的 OBS 热键绑定数据（JSON 数组），确保切换方案后快捷键能正确恢复
		OBSDataArrayAutoRelease bindings =
			static_cast<obs_data_array_t *>(
				item->data(Bindings).value<void *>());
		if (bindings) {
			size_t count = obs_data_array_count(bindings);
			QJsonArray bindingsJsonArray;
			for (size_t j = 0; j < count; j++) {
				obs_data_t *binding = obs_data_array_item(bindings, j);
				if (binding) {
					const char *jsonStr = obs_data_get_json(binding);
					if (jsonStr) {
						QJsonDocument bindingDoc = QJsonDocument::fromJson(jsonStr);
						if (bindingDoc.isObject()) {
							bindingsJsonArray.append(bindingDoc.object());
						}
					}
					obs_data_release(binding);
				}
			}
			if (!bindingsJsonArray.isEmpty()) {
				markerObj["bindings"] = bindingsJsonArray;
			}
			
			// 同时保存可读的快捷键字符串（用于外部配置和调试）
			if (count > 0) {
				obs_data_t *binding = obs_data_array_item(bindings, 0);
				if (binding) {
					const char *key = obs_data_get_string(binding, "key");
					bool shift = obs_data_get_bool(binding, "shift");
					bool control = obs_data_get_bool(binding, "control");
					bool alt = obs_data_get_bool(binding, "alt");
					bool command = obs_data_get_bool(binding, "command");
					
					QStringList parts;
					if (control) parts << "Ctrl";
					if (shift) parts << "Shift";
					if (alt) parts << "Alt";
					if (command) parts << "Cmd";
					if (key && *key) {
						QString keyStr = QString(key).toUpper();
						if (keyStr.startsWith("OBS_KEY_"))
							keyStr = keyStr.mid(8);
						parts << keyStr;
					}
					markerObj["hotkey"] = parts.join("+");
					obs_data_release(binding);
				}
			}
		}
		
		markersArray.append(markerObj);
	}
	
	QJsonObject profileObj;
	profileObj["name"] = profileName;
	profileObj["description"] = description;
	profileObj["version"] = "3.0";
	profileObj["enableComments"] = g_enableComments;
	profileObj["createdAt"] = QDateTime::currentDateTime().toString(Qt::ISODate);
	profileObj["markers"] = markersArray;
	
	QJsonDocument doc(profileObj);
	QString filePath = getProfilesDir() + "/" + profileName + ".json";
	QFile file(filePath);
	if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
		file.write(doc.toJson(QJsonDocument::Indented));
		file.close();
		currentProfileName = profileName;
		plog(LOG_INFO, "Profile saved: %s", qUtf8Printable(profileName));
		
		// 保存活跃方案名，供 PR 插件读取
		QFile activeFile(getProfilesDir() + "/_active.txt");
		if (activeFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
			activeFile.write(profileName.toUtf8());
			activeFile.close();
		}
	}
}

void ChapterHotkeyUI::loadProfile(const QString &profileName)
{
	QString filePath = getProfilesDir() + "/" + profileName + ".json";
	QFile file(filePath);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
		plog(LOG_WARNING, "Cannot open profile: %s", qUtf8Printable(profileName));
		return;
	}
	
	QByteArray data = file.readAll();
	file.close();
	
	QJsonDocument doc = QJsonDocument::fromJson(data);
	if (!doc.isObject()) return;
	
	QJsonObject profileObj = doc.object();
	QJsonArray markersArray = profileObj["markers"].toArray();
	
	// 加载时屏蔽 itemChanged 信号，避免触发不必要的自动保存
	ui->listWidget->blockSignals(true);
	
	// 清空当前列表
	// 注意：delete ChapterHotkeyItem 会 obs_hotkey_unregister 注销旧热键
	while (ui->listWidget->count() > 0) {
		delete ui->listWidget->takeItem(0);
	}
	
	// 加载标记
	for (int i = 0; i < markersArray.size(); i++) {
		QJsonObject markerObj = markersArray[i].toObject();
		QString name = markerObj["name"].toString();
		QString uuid = markerObj["uuid"].toString();
		QString color = markerObj["color"].toString();
		
		if (name.isEmpty()) continue;
		if (uuid.isEmpty()) {
			uuid = "chapter_hotkey_" + QUuid::createUuid().toString(QUuid::WithoutBraces);
		}
		
		OBSDataArrayAutoRelease bindings = nullptr;
		
		// 优先从完整的 bindings JSON 数组恢复（v3.0 格式）
		if (markerObj.contains("bindings") && markerObj["bindings"].isArray()) {
			QJsonArray bindingsJsonArray = markerObj["bindings"].toArray();
			if (!bindingsJsonArray.isEmpty()) {
				bindings = obs_data_array_create();
				for (int j = 0; j < bindingsJsonArray.size(); j++) {
					QJsonObject bindObj = bindingsJsonArray[j].toObject();
					QJsonDocument bindDoc(bindObj);
					QByteArray bindJson = bindDoc.toJson(QJsonDocument::Compact);
					OBSDataAutoRelease binding = obs_data_create_from_json(bindJson.constData());
					if (binding) {
						obs_data_array_push_back(bindings, binding);
					}
				}
				plog(LOG_INFO, "Marker '%s': restored %d bindings from profile data",
					qUtf8Printable(name), (int)obs_data_array_count(bindings));
			}
		}
		
		// 回退：从旧版 hotkey 字符串解析（v2.0 兼容）
		if (!bindings || obs_data_array_count(bindings) == 0) {
			QString hotkeyStr = markerObj["hotkey"].toString();
			if (!hotkeyStr.isEmpty() && hotkeyStr != "NONE") {
				bindings = obs_data_array_create();
				OBSDataAutoRelease binding = obs_data_create();
				
				QStringList parts = hotkeyStr.split("+");
				QString keyStr;
				bool shift = false, control = false, alt = false, command = false;
				
				for (const QString &part : parts) {
					QString p = part.trimmed().toUpper();
					if (p == "CTRL" || p == "CONTROL") control = true;
					else if (p == "SHIFT") shift = true;
					else if (p == "ALT") alt = true;
					else if (p == "CMD" || p == "COMMAND") command = true;
					else {
						if (p.startsWith("NUM"))
							keyStr = "OBS_KEY_NUMPAD" + p.mid(3);
						else
							keyStr = "OBS_KEY_" + p;
					}
				}
				
				obs_data_set_string(binding, "key", keyStr.toUtf8().constData());
				obs_data_set_bool(binding, "shift", shift);
				obs_data_set_bool(binding, "control", control);
				obs_data_set_bool(binding, "alt", alt);
				obs_data_set_bool(binding, "command", command);
				obs_data_array_push_back(bindings, binding);
				plog(LOG_INFO, "Marker '%s': parsed hotkey string '%s' (legacy format)",
					qUtf8Printable(name), qUtf8Printable(hotkeyStr));
			}
		}
		
		auto hkItem = new ChapterHotkeyItem(uuid, name.toUtf8().constData(), bindings,
			color.isEmpty() ? "#718637" : color);
		ui->listWidget->addItem(hkItem);
	}
	
	// 恢复注释开关
	if (profileObj.contains("enableComments")) {
		g_enableComments = profileObj["enableComments"].toBool();
		if (enableCommentsCheckBox)
			enableCommentsCheckBox->setChecked(g_enableComments);
	}
	
	ui->listWidget->sortItems();
	
	// 恢复信号
	ui->listWidget->blockSignals(false);
	
	currentProfileName = profileName;
	
	plog(LOG_INFO, "Profile loaded: %s with %d markers", qUtf8Printable(profileName), ui->listWidget->count());
}

void ChapterHotkeyUI::deleteProfile(const QString &profileName)
{
	QString filePath = getProfilesDir() + "/" + profileName + ".json";
	QFile::remove(filePath);
	if (currentProfileName == profileName) {
		currentProfileName.clear();
	}
	plog(LOG_INFO, "Profile deleted: %s", qUtf8Printable(profileName));
}

QStringList ChapterHotkeyUI::getProfileNames()
{
	QDir dir(getProfilesDir());
	QStringList filters;
	filters << "*.json";
	QStringList files = dir.entryList(filters, QDir::Files, QDir::Name);
	QStringList names;
	for (const QString &f : files) {
		names.append(f.left(f.length() - 5)); // 去掉 .json
	}
	return names;
}

void ChapterHotkeyUI::refreshProfileCombo()
{
	if (!profileCombo) return;
	
	profileCombo->blockSignals(true);
	profileCombo->clear();
	
	QStringList profiles = getProfileNames();
	for (const QString &p : profiles) {
		profileCombo->addItem(p);
	}
	
	// 选中当前方案（默认"预设"）
	QString targetProfile = currentProfileName.isEmpty() ? QString::fromUtf8("预设") : currentProfileName;
	int idx = profileCombo->findText(targetProfile);
	if (idx >= 0) {
		profileCombo->setCurrentIndex(idx);
	} else if (profileCombo->count() > 0) {
		profileCombo->setCurrentIndex(0);
	}
	
	profileCombo->blockSignals(false);
}

void ChapterHotkeyUI::restoreProfileSelection()
{
	// 如果当前没有设置方案名，默认使用"预设"
	if (currentProfileName.isEmpty()) {
		currentProfileName = QString::fromUtf8("预设");
	}
	
	// 检查该方案文件是否存在
	QString filePath = getProfilesDir() + "/" + currentProfileName + ".json";
	if (!QFile::exists(filePath)) {
		// 方案文件不存在，回退到"预设"
		currentProfileName = QString::fromUtf8("预设");
		plog(LOG_INFO, "Profile file not found, falling back to default preset");
	}
	
	refreshProfileCombo();
	
	// 加载方案
	if (!currentProfileName.isEmpty()) {
		QString profilePath = getProfilesDir() + "/" + currentProfileName + ".json";
		if (QFile::exists(profilePath)) {
			loadProfile(currentProfileName);
		}
	}
	
	plog(LOG_INFO, "Restored profile selection: %s", qUtf8Printable(currentProfileName));
}

void ChapterHotkeyUI::autoSaveCurrentProfile()
{
	// 如果当前没有选中方案，默认保存到"预设"
	if (currentProfileName.isEmpty()) {
		currentProfileName = QString::fromUtf8("预设");
	}
	
	saveCurrentAsProfile(currentProfileName);
	plog(LOG_INFO, "Auto-saved to profile: %s (%d markers)",
		qUtf8Printable(currentProfileName), ui->listWidget->count());
}

void ChapterHotkeyUI::onProfileComboChanged(int index)
{
	if (index < 0) return;
	
	QString profileName = profileCombo->currentText();
	if (profileName.isEmpty()) return;
	
	// 如果选中的就是当前方案，不重复加载
	if (profileName == currentProfileName) return;
	
	// 直接加载方案，无需二次确认
	loadProfile(profileName);
}

void ChapterHotkeyUI::onSaveProfileClicked()
{
	bool ok;
	QString name = QInputDialog::getText(this, "新建方案",
		"请输入新方案名称：",
		QLineEdit::Normal, "", &ok);
	
	if (!ok || name.trimmed().isEmpty()) return;
	name = name.trimmed();
	
	// 检查是否已存在
	QStringList existing = getProfileNames();
	if (existing.contains(name)) {
		auto ret = QMessageBox::question(this, "覆盖方案",
			QString("方案「%1」已存在，是否覆盖？").arg(name),
			QMessageBox::Yes | QMessageBox::No);
		if (ret != QMessageBox::Yes) return;
	}
	
	// 清空当前标记列表（新建空白方案）
	ui->listWidget->blockSignals(true);
	while (ui->listWidget->count() > 0) {
		delete ui->listWidget->takeItem(0);
	}
	ui->listWidget->blockSignals(false);
	
	// 保存为空白方案
	currentProfileName = name;
	saveCurrentAsProfile(name);
	refreshProfileCombo();
	
	plog(LOG_INFO, "New empty profile created: %s", qUtf8Printable(name));
}

void ChapterHotkeyUI::onDeleteProfileClicked()
{
	if (profileCombo->currentIndex() < 0 || profileCombo->currentText().isEmpty()) {
		QMessageBox::warning(this, "提示", "请先选择要删除的方案。");
		return;
	}
	
	QString name = profileCombo->currentText();
	auto ret = QMessageBox::question(this, "删除方案",
		QString("确定要删除方案「%1」吗？此操作不可撤销。").arg(name),
		QMessageBox::Yes | QMessageBox::No);
	
	if (ret == QMessageBox::Yes) {
		deleteProfile(name);
		
		// 删除后自动切换到其他方案
		QStringList remaining = getProfileNames();
		if (!remaining.isEmpty()) {
			currentProfileName = remaining.first();
			loadProfile(currentProfileName);
		} else {
			// 没有方案了，创建默认预设
			currentProfileName = QString::fromUtf8("预设");
			ui->listWidget->blockSignals(true);
			while (ui->listWidget->count() > 0)
				delete ui->listWidget->takeItem(0);
			ui->listWidget->blockSignals(false);
			saveCurrentAsProfile(currentProfileName);
		}
		refreshProfileCombo();
	}
}

void ChapterHotkeyUI::onRenameProfileClicked()
{
	if (profileCombo->currentIndex() < 0 || profileCombo->currentText().isEmpty()) {
		QMessageBox::warning(this, "提示", "请先选择要重命名的方案。");
		return;
	}
	
	QString oldName = profileCombo->currentText();
	
	bool ok;
	QString newName = QInputDialog::getText(this, "重命名方案",
		QString("将方案「%1」重命名为：").arg(oldName),
		QLineEdit::Normal, oldName, &ok);
	
	if (!ok || newName.trimmed().isEmpty()) return;
	newName = newName.trimmed();
	
	if (newName == oldName) return;
	
	// 检查新名称是否已存在
	QStringList existing = getProfileNames();
	if (existing.contains(newName)) {
		QMessageBox::warning(this, "重命名失败",
			QString("方案「%1」已存在，请选择其他名称。").arg(newName));
		return;
	}
	
	// 重命名文件
	QString oldPath = getProfilesDir() + "/" + oldName + ".json";
	QString newPath = getProfilesDir() + "/" + newName + ".json";
	
	if (QFile::rename(oldPath, newPath)) {
		// 更新方案文件内部的名称字段
		QFile file(newPath);
		if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
			QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
			file.close();
			if (doc.isObject()) {
				QJsonObject obj = doc.object();
				obj["name"] = newName;
				if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
					file.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
					file.close();
				}
			}
		}
		
		if (currentProfileName == oldName) {
			currentProfileName = newName;
		}
		refreshProfileCombo();
		plog(LOG_INFO, "Profile renamed: %s -> %s", qUtf8Printable(oldName), qUtf8Printable(newName));
	} else {
		QMessageBox::warning(this, "重命名失败", "文件重命名失败，请检查文件权限。");
	}
}

// ============================================================================
// 刷新快捷键按钮：重新从 OBS 获取所有热键的绑定信息并刷新显示
// ============================================================================
void ChapterHotkeyUI::onRefreshHotkeysClicked()
{
	refreshAllHotkeyDisplays();
	plog(LOG_INFO, "Hotkey displays refreshed manually");
}

void ChapterHotkeyUI::refreshAllHotkeyDisplays()
{
	for (int i = 0; i < ui->listWidget->count(); i++) {
		auto *item = dynamic_cast<ChapterHotkeyItem *>(ui->listWidget->item(i));
		if (!item) continue;
		
		// 刷新显示文本（包括快捷键）
		// data(Bindings) 会动态调用 obs_hotkey_save() 获取最新绑定数据
		item->updateDisplayText();
	}
	
	// 同步保存到当前方案
	autoSaveCurrentProfile();
}

// ============================================================================
// HotkeyItemDelegate - 标记名称左对齐，快捷键右对齐灰色显示
// ============================================================================
void HotkeyItemDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
			       const QModelIndex &index) const
{
	// 先让默认绘制处理背景、选中高亮、图标等
	QStyleOptionViewItem opt = option;
	initStyleOption(&opt, index);
	
	// 获取快捷键文本
	QString hotkeyText = index.data(HotkeyText).toString();
	
	painter->save();
	
	// 绘制背景（选中/hover等状态）
	QStyle *style = opt.widget ? opt.widget->style() : QApplication::style();
	
	// 清空文本，让 style 只画背景和图标
	QString originalText = opt.text;
	opt.text = QString();
	style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);
	
	// 计算文本区域（排除图标和装饰区域）
	QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &opt, opt.widget);
	int padding = 4;
	textRect.adjust(padding, 0, -padding, 0);
	
	// 设置文本颜色
	QPalette::ColorGroup cg = (opt.state & QStyle::State_Enabled) 
		? QPalette::Normal : QPalette::Disabled;
	if (opt.state & QStyle::State_Selected) {
		painter->setPen(opt.palette.color(cg, QPalette::HighlightedText));
	} else {
		painter->setPen(opt.palette.color(cg, QPalette::Text));
	}
	
	// 绘制标记名称（左对齐）
	painter->setFont(opt.font);
	painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, originalText);
	
	// 绘制快捷键（右对齐，灰色，稍小字体）
	if (!hotkeyText.isEmpty()) {
		QString displayHotkey = "[" + hotkeyText + "]";
		
		QFont hotkeyFont = opt.font;
		hotkeyFont.setPointSizeF(hotkeyFont.pointSizeF() * 0.85);
		painter->setFont(hotkeyFont);
		
		if (opt.state & QStyle::State_Selected) {
			QColor c = opt.palette.color(cg, QPalette::HighlightedText);
			c.setAlpha(180);
			painter->setPen(c);
		} else {
			painter->setPen(QColor(140, 140, 140));
		}
		
		painter->drawText(textRect, Qt::AlignRight | Qt::AlignVCenter, displayHotkey);
	}
	
	// 绘制列表项底部分隔线（替代 QListWidget stylesheet，避免破坏选中高亮）
	painter->setPen(QPen(QColor(0x2a, 0x2a, 0x2a), 1));
	int y = option.rect.bottom();
	painter->drawLine(option.rect.left(), y, option.rect.right(), y);
	
	painter->restore();
}

QSize HotkeyItemDelegate::sizeHint(const QStyleOptionViewItem &option,
				    const QModelIndex &index) const
{
	QSize size = QStyledItemDelegate::sizeHint(option, index);
	// 确保行高足够
	if (size.height() < 24)
		size.setHeight(24);
	return size;
}

// ============================================================================
// 导入/导出功能
// ============================================================================
void ChapterHotkeyUI::exportConfig(const QString &filePath)
{
	QJsonArray markersArray;
	for (int i = 0; i < ui->listWidget->count(); i++) {
		auto item = ui->listWidget->item(i);
		QJsonObject markerObj;
		markerObj["name"] = item->data(Name).toString();
		markerObj["uuid"] = item->data(HotkeyId).toString();
		markerObj["color"] = item->data(Color).toString();
		
		OBSDataArrayAutoRelease bindings =
			static_cast<obs_data_array_t *>(
				item->data(Bindings).value<void *>());
		if (bindings) {
			size_t count = obs_data_array_count(bindings);
			
			// 保存完整的 OBS 热键绑定数据
			QJsonArray bindingsJsonArray;
			for (size_t j = 0; j < count; j++) {
				obs_data_t *binding = obs_data_array_item(bindings, j);
				if (binding) {
					const char *jsonStr = obs_data_get_json(binding);
					if (jsonStr) {
						QJsonDocument bindingDoc = QJsonDocument::fromJson(jsonStr);
						if (bindingDoc.isObject()) {
							bindingsJsonArray.append(bindingDoc.object());
						}
					}
					obs_data_release(binding);
				}
			}
			if (!bindingsJsonArray.isEmpty()) {
				markerObj["bindings"] = bindingsJsonArray;
			}
			
			// 同时保存可读的快捷键字符串
			if (count > 0) {
				obs_data_t *binding = obs_data_array_item(bindings, 0);
				if (binding) {
					const char *key = obs_data_get_string(binding, "key");
					bool shift = obs_data_get_bool(binding, "shift");
					bool control = obs_data_get_bool(binding, "control");
					bool alt = obs_data_get_bool(binding, "alt");
					bool command = obs_data_get_bool(binding, "command");
					
					QStringList parts;
					if (control) parts << "Ctrl";
					if (shift) parts << "Shift";
					if (alt) parts << "Alt";
					if (command) parts << "Cmd";
					if (key && *key) {
						QString keyStr = QString(key).toUpper();
						if (keyStr.startsWith("OBS_KEY_"))
							keyStr = keyStr.mid(8);
						parts << keyStr;
					}
					markerObj["hotkey"] = parts.join("+");
					obs_data_release(binding);
				}
			}
		}
		
		markersArray.append(markerObj);
	}
	
	QJsonObject exportObj;
	exportObj["version"] = "3.0";
	exportObj["pluginName"] = "obs-named-chapter-hotkeys";
	exportObj["exportedAt"] = QDateTime::currentDateTime().toString(Qt::ISODate);
	exportObj["profile"] = currentProfileName;
	exportObj["enableComments"] = g_enableComments;
	exportObj["markers"] = markersArray;
	
	QJsonDocument doc(exportObj);
	QFile file(filePath);
	if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
		file.write(doc.toJson(QJsonDocument::Indented));
		file.close();
		plog(LOG_INFO, "Config exported to: %s", qUtf8Printable(filePath));
	}
}

void ChapterHotkeyUI::importConfig(const QString &filePath)
{
	QFile file(filePath);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
		QMessageBox::warning(this, "导入失败", "无法打开文件：" + filePath);
		return;
	}
	
	QByteArray data = file.readAll();
	file.close();
	
	QJsonDocument doc = QJsonDocument::fromJson(data);
	if (!doc.isObject()) {
		QMessageBox::warning(this, "导入失败", "文件格式无效。");
		return;
	}
	
	QJsonObject importObj = doc.object();
	QJsonArray markersArray = importObj["markers"].toArray();
	
	if (markersArray.isEmpty()) {
		QMessageBox::warning(this, "导入失败", "配置文件中没有标记数据。");
		return;
	}
	
	// 询问是替换还是合并
	auto ret = QMessageBox::question(this, "导入配置",
		QString("检测到 %1 个标记。\n\n"
			"点击「Yes」替换当前所有标记\n"
			"点击「No」合并到当前标记列表").arg(markersArray.size()),
		QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
	
	if (ret == QMessageBox::Cancel) return;
	
	bool replace = (ret == QMessageBox::Yes);
	
	// 屏蔽信号，避免导入过程中触发不必要的自动保存
	ui->listWidget->blockSignals(true);
	
	// 收集现有 UUID（合并模式下用于去重）
	QSet<QString> existingUUIDs;
	if (!replace) {
		for (int i = 0; i < ui->listWidget->count(); i++) {
			existingUUIDs.insert(ui->listWidget->item(i)->data(HotkeyId).toString());
		}
	}
	
	if (replace) {
		while (ui->listWidget->count() > 0) {
			delete ui->listWidget->takeItem(0);
		}
	}
	
	int imported = 0;
	for (int i = 0; i < markersArray.size(); i++) {
		QJsonObject markerObj = markersArray[i].toObject();
		QString name = markerObj["name"].toString();
		QString uuid = markerObj["uuid"].toString();
		QString color = markerObj["color"].toString();
		
		if (name.isEmpty()) continue;
		// 合并模式下，如果 UUID 已存在则重新生成，避免热键 ID 冲突
		if (uuid.isEmpty() || (!replace && existingUUIDs.contains(uuid))) {
			uuid = "chapter_hotkey_" + QUuid::createUuid().toString(QUuid::WithoutBraces);
		}
		
		OBSDataArrayAutoRelease bindings = nullptr;
		
		// 优先从完整的 bindings JSON 数组恢复（v3.0 格式）
		if (markerObj.contains("bindings") && markerObj["bindings"].isArray()) {
			QJsonArray bindingsJsonArray = markerObj["bindings"].toArray();
			if (!bindingsJsonArray.isEmpty()) {
				bindings = obs_data_array_create();
				for (int j = 0; j < bindingsJsonArray.size(); j++) {
					QJsonObject bindObj = bindingsJsonArray[j].toObject();
					QJsonDocument bindDoc(bindObj);
					QByteArray bindJson = bindDoc.toJson(QJsonDocument::Compact);
					OBSDataAutoRelease binding = obs_data_create_from_json(bindJson.constData());
					if (binding) {
						obs_data_array_push_back(bindings, binding);
					}
				}
			}
		}
		
		// 回退：从旧版 hotkey 字符串解析（v2.0 兼容）
		if (!bindings || obs_data_array_count(bindings) == 0) {
			QString hotkeyStr = markerObj["hotkey"].toString();
			if (!hotkeyStr.isEmpty() && hotkeyStr != "NONE") {
				bindings = obs_data_array_create();
				OBSDataAutoRelease binding = obs_data_create();
				
				QStringList parts = hotkeyStr.split("+");
				QString keyStr;
				bool shift = false, control = false, alt = false, command = false;
				
				for (const QString &part : parts) {
					QString p = part.trimmed().toUpper();
					if (p == "CTRL" || p == "CONTROL") control = true;
					else if (p == "SHIFT") shift = true;
					else if (p == "ALT") alt = true;
					else if (p == "CMD" || p == "COMMAND") command = true;
					else {
						if (p.startsWith("NUM"))
							keyStr = "OBS_KEY_NUMPAD" + p.mid(3);
						else
							keyStr = "OBS_KEY_" + p;
					}
				}
				
				obs_data_set_string(binding, "key", keyStr.toUtf8().constData());
				obs_data_set_bool(binding, "shift", shift);
				obs_data_set_bool(binding, "control", control);
				obs_data_set_bool(binding, "alt", alt);
				obs_data_set_bool(binding, "command", command);
				obs_data_array_push_back(bindings, binding);
			}
		}
		
		auto hkItem = new ChapterHotkeyItem(uuid, name.toUtf8().constData(), bindings,
			color.isEmpty() ? "#718637" : color);
		ui->listWidget->addItem(hkItem);
		imported++;
	}
	
	// 恢复注释设置
	if (importObj.contains("enableComments")) {
		g_enableComments = importObj["enableComments"].toBool();
		if (enableCommentsCheckBox)
			enableCommentsCheckBox->setChecked(g_enableComments);
	}
	
	if (importObj.contains("profile")) {
		currentProfileName = importObj["profile"].toString();
	}
	
	ui->listWidget->sortItems();
	
	// 恢复信号
	ui->listWidget->blockSignals(false);
	
	autoSaveCurrentProfile();
	refreshProfileCombo();
	
	QMessageBox::information(this, "导入成功",
		QString("已%1 %2 个标记。").arg(replace ? "导入" : "合并").arg(imported));
}

void ChapterHotkeyUI::onExportClicked()
{
	QString defaultName = currentProfileName.isEmpty() ? QString::fromUtf8("预设") : currentProfileName;
	
	QString filePath = QFileDialog::getSaveFileName(this,
		"导出标记配置", defaultName + ".json",
		"JSON 文件 (*.json);;所有文件 (*.*)");
	
	if (!filePath.isEmpty()) {
		exportConfig(filePath);
		QMessageBox::information(this, "导出成功",
			QString("配置已导出到：\n%1").arg(filePath));
	}
}

void ChapterHotkeyUI::onImportClicked()
{
	QString filePath = QFileDialog::getOpenFileName(this,
		"导入标记配置", "",
		"JSON 文件 (*.json);;所有文件 (*.*)");
	
	if (!filePath.isEmpty()) {
		importConfig(filePath);
	}
}

// ============================================================================
// OBS 热键数据保存/加载
// ============================================================================
void ChapterHotkeyUI::LoadHotkeys(obs_data_t *data)
{
	ui->listWidget->clear();

	obs_data_item *item = obs_data_first(data);

	while (item) {
		const char *id = obs_data_item_get_name(item);
		OBSDataAutoRelease hk = obs_data_item_get_obj(item);

		const char *name = obs_data_get_string(hk, "name");
		const char *color = obs_data_get_string(hk, "color");
		OBSDataArrayAutoRelease bindings =
			obs_data_get_array(hk, "bindings");

		auto hkItem = new ChapterHotkeyItem(id, name, bindings, color && *color ? color : "#718637");
		ui->listWidget->addItem(hkItem);

		obs_data_item_next(&item);
	}
}

void ChapterHotkeyUI::SaveHotkeys(obs_data_t *data)
{
	obs_data_clear(data);

	for (int i = 0; i < ui->listWidget->count(); i++) {
		auto item = ui->listWidget->item(i);

		auto name = item->data(Name).toString();
		auto uuid = item->data(HotkeyId).toString();
		auto color = item->data(Color).toString();
		OBSDataArrayAutoRelease bindings =
			static_cast<obs_data_array_t *>(
				item->data(Bindings).value<void *>());

		OBSDataAutoRelease hk = obs_data_create();
		obs_data_set_string(hk, "name", name.toUtf8().constData());
		obs_data_set_string(hk, "color", color.toUtf8().constData());
		obs_data_set_array(hk, "bindings", bindings);
		obs_data_set_obj(data, uuid.toUtf8().constData(), hk);
	}
	
	// 同时保存到当前方案文件（确保 OBS 设置中修改的快捷键绑定也保存到方案）
	autoSaveCurrentProfile();
	plog(LOG_INFO, "Profile also saved during OBS save: %s", qUtf8Printable(currentProfileName));
}

// ============================================================================
// ChapterHotkeyItem
// ============================================================================
ChapterHotkeyItem::ChapterHotkeyItem(const QString &id, const char *name,
				     obs_data_array_t *bindings, const QString &color)
	: QListWidgetItem(nullptr),
	  chapterName(name),
	  hotkeyUUID(id),
	  color(color)
{
	setText(name);

	QString formattedName = QString("添加章节标记 '%1'").arg(name);

	hotkey = obs_hotkey_register_frontend(
		id.toUtf8().constData(), formattedName.toUtf8().constData(),
		ChapterHotkeyItem::HotkeyPressed, this);

	if (bindings)
		obs_hotkey_load(hotkey, bindings);
	
	updateDisplayText();
}

ChapterHotkeyItem::~ChapterHotkeyItem()
{
	obs_hotkey_unregister(hotkey);
}

void ChapterHotkeyItem::updateDisplayText()
{
	QString hotkeyText = getHotkeyText();
	
	// 名称只设置标记名
	setText(QString::fromStdString(chapterName));
	
	// 快捷键文本存储到自定义角色，供 HotkeyItemDelegate 绘制（右对齐）
	if (hotkeyText.isEmpty() || hotkeyText == "None") {
		QListWidgetItem::setData(HotkeyText, QString());
	} else {
		QListWidgetItem::setData(HotkeyText, hotkeyText);
	}
	
	if (!color.isEmpty() && color != "none") {
		QColor circleColor = getColorFromHex(color);
		int diameter = 12;
		QPixmap pixmap(diameter, diameter);
		pixmap.fill(Qt::transparent);
		QPainter painter(&pixmap);
		painter.setRenderHint(QPainter::Antialiasing);
		painter.setBrush(circleColor);
		painter.setPen(QPen(QColor("#333333"), 1));
		painter.drawEllipse(0, 0, diameter - 1, diameter - 1);
		painter.end();
		setIcon(QIcon(pixmap));
	} else {
		setIcon(QIcon());
	}
	
	setToolTip(QString("Hotkey: %1").arg(hotkeyText));
}

QString ChapterHotkeyItem::getHotkeyText() const
{
	obs_data_array_t *bindings = obs_hotkey_save(hotkey);
	if (!bindings) return "None";
	
	size_t count = obs_data_array_count(bindings);
	if (count == 0) {
		obs_data_array_release(bindings);
		return "None";
	}
	
	QStringList results;
	for (size_t i = 0; i < count; i++) {
		obs_data_t *binding = obs_data_array_item(bindings, i);
		if (!binding) continue;
		
		const char *key = obs_data_get_string(binding, "key");
		bool shift = obs_data_get_bool(binding, "shift");
		bool control = obs_data_get_bool(binding, "control");
		bool alt = obs_data_get_bool(binding, "alt");
		bool command = obs_data_get_bool(binding, "command");
		
		QStringList parts;
		if (control) parts << "Ctrl";
		if (shift) parts << "Shift";
		if (alt) parts << "Alt";
		if (command) parts << "Cmd";
		if (key && *key) {
			QString keyStr = QString(key).toUpper();
			if (keyStr.startsWith("OBS_KEY_NUMPAD")) {
				keyStr = "NUM" + keyStr.mid(14);
			} else if (keyStr.startsWith("OBS_KEY_")) {
				keyStr = keyStr.mid(8);
			}
			parts << keyStr;
		}
		
		if (!parts.isEmpty()) {
			results.append(parts.join("+"));
		}
		obs_data_release(binding);
	}
	obs_data_array_release(bindings);
	
	if (results.isEmpty()) return "None";
	return results.join(", ");
}

static QString g_pendingChapterName;
static QString g_pendingColorHex;

static void ShowCommentDialog();

// ============================================================================
// 热键按下处理 - 改进重入保护
// ============================================================================
void ChapterHotkeyItem::HotkeyPressed(void *_this, obs_hotkey_id,
			      obs_hotkey_t *, bool pressed)
{
	auto hk = static_cast<ChapterHotkeyItem *>(_this);

	if (pressed) {
		// 检查OBS是否正在录制，只有在录制时才启用标记功能
		if (!obs_frontend_recording_active()) {
			return;
		}
		
		// 【改进】使用原子标志进行严格的重入保护
		// 如果注释窗口已打开或正在创建中，直接忽略
		if (ChapterWithCommentDialog::IsDialogOpen()) {
			plog(LOG_INFO, "Hotkey ignored: comment dialog is already open");
			return;
		}
		
		if (g_showDialogPending.exchange(true)) {
			plog(LOG_INFO, "Hotkey ignored: dialog creation already pending");
			return;
		}
		
		g_pendingChapterName = QString::fromStdString(hk->getChapterName());
		g_pendingColorHex = hk->getColor();
		
		// 判断是否启用注释功能
		if (!g_enableComments) {
			// 未启用注释功能：直接添加普通标记
			string chapterName = hk->getChapterName();
			if (!g_pendingColorHex.isEmpty() && g_pendingColorHex != "none") {
				QString colorName = getColorName(g_pendingColorHex);
				chapterName = "(" + colorName.toStdString() + ") " + chapterName;
			}
			obs_frontend_recording_add_chapter(chapterName.c_str());
			plog(LOG_INFO, "Chapter marker added (no comment): '%s'", chapterName.c_str());
			
			// 通知实时预览面板
			if (g_livePanel) {
				QMetaObject::invokeMethod(g_livePanel, [hk]() {
					if (g_livePanel) {
						g_livePanel->addMarker(
							QString::fromStdString(hk->getChapterName()),
							hk->getColor(),
							"");
					}
				}, Qt::QueuedConnection);
			}
			
			g_showDialogPending.store(false);
			return;
		}
		
		// 启用注释功能：添加带@后缀的标记，用于与普通标记区分
		string initialChapterName = hk->getChapterName();
		initialChapterName = initialChapterName + "@";
		if (!g_pendingColorHex.isEmpty() && g_pendingColorHex != "none") {
			QString colorName = getColorName(g_pendingColorHex);
			initialChapterName = "(" + colorName.toStdString() + ") " + initialChapterName;
		}
		obs_frontend_recording_add_chapter(initialChapterName.c_str());
		plog(LOG_INFO, "Initial comment marker added: '%s', opening comment dialog...", initialChapterName.c_str());
		
		// 【改进】使用 QMetaObject::invokeMethod 安全地在 UI 线程中打开对话框
		QMetaObject::invokeMethod(QCoreApplication::instance(), []() {
			ShowCommentDialog();
			g_showDialogPending.store(false);
		}, Qt::QueuedConnection);
	}
}

static void ShowCommentDialog()
{
	// 二次检查：确保不会重复打开
	if (ChapterWithCommentDialog::IsDialogOpen()) {
		plog(LOG_WARNING, "ShowCommentDialog: dialog already open, aborting");
		return;
	}
	
	plog(LOG_INFO, "ShowCommentDialog: opening for marker '%s'", qUtf8Printable(g_pendingChapterName));
	string nameInput = g_pendingChapterName.toStdString();
	string commentInput;
	QString selectedColor = g_pendingColorHex; // 默认使用触发时的颜色
	
	auto window = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	bool accepted = ChapterWithCommentDialog::AskForNameAndComment(
		window, 
		"添加标记注释", 
		"", 
		nameInput, 
		commentInput,
		g_pendingChapterName,
		&selectedColor); // 传入指针以获取选中的颜色

	if (accepted) {
		string finalChapterName = nameInput;
		// 即使没有注释，也添加@后缀以匹配前面的标记
		if (!commentInput.empty()) {
			finalChapterName = nameInput + "@" + commentInput;
		} else {
			finalChapterName = nameInput + "@";
		}
		
		// 使用选中的颜色（如果用户在下拉框中选择了不同的标记，颜色也会随之改变）
		if (!selectedColor.isEmpty() && selectedColor != "none") {
			QString colorName = getColorName(selectedColor);
			finalChapterName = "(" + colorName.toStdString() + ") " + finalChapterName;
		}
		obs_frontend_recording_add_chapter(finalChapterName.c_str());
		plog(LOG_INFO, "Comment dialog accepted: marker='%s', comment='%s'", nameInput.c_str(), commentInput.c_str());
		
		// 通知实时预览面板
		if (g_livePanel) {
			QString markerName = QString::fromStdString(nameInput);
			QString markerColor = selectedColor;
			QString markerComment = QString::fromStdString(commentInput);
			QMetaObject::invokeMethod(g_livePanel, [markerName, markerColor, markerComment]() {
				if (g_livePanel) {
					g_livePanel->addMarker(markerName, markerColor, markerComment);
				}
			}, Qt::QueuedConnection);
		}
	} else {
		// 用户取消：创建标记 A2@###，表示这组标记需要被删除
		plog(LOG_INFO, "Comment dialog cancelled for marker '%s'", nameInput.c_str());
		string cancelChapterName = nameInput + "@###";
		if (!selectedColor.isEmpty() && selectedColor != "none") {
			QString colorName = getColorName(selectedColor);
			cancelChapterName = "(" + colorName.toStdString() + ") " + cancelChapterName;
		}
		obs_frontend_recording_add_chapter(cancelChapterName.c_str());
	}
}

// ============================================================================
// ChapterHotkeyItem data/setData
// ============================================================================
QVariant ChapterHotkeyItem::data(int role) const
{
	if (role == Name)
		return QString::fromStdString(chapterName);
	if (role == HotkeyId)
		return hotkeyUUID;
	if (role == Bindings) {
		obs_data_array_t *hk = obs_hotkey_save(hotkey);
		return QVariant::fromValue(static_cast<void *>(hk));
	}
	if (role == Color)
		return color;

	return QListWidgetItem::data(role);
}

void ChapterHotkeyItem::setData(int role, const QVariant &value)
{
	if (role == Name || role == Qt::EditRole) {
		QString newName = value.toString();
		QString formattedName = QString("添加章节标记 '%1'").arg(newName);

		chapterName = newName.toStdString();
		obs_hotkey_set_description(hotkey,
					   formattedName.toUtf8().constData());

		// 更新显示文本，包含颜色前缀
		updateDisplayText();
	} else if (role == Color) {
		color = value.toString();
		// 更新显示文本，包含颜色前缀
		updateDisplayText();
	} else {
		QListWidgetItem::setData(role, value);
	}
}

// ============================================================================
// ChapterWithCommentDialog - 改进重入保护
// ============================================================================
ChapterWithCommentDialog::ChapterWithCommentDialog(QWidget *parent) : QDialog(parent)
{
	setModal(true);
	setWindowModality(Qt::WindowModality::WindowModal);
	setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint | Qt::WindowStaysOnTopHint);
	setFixedWidth(280);
	setMinimumHeight(190);
	resize(280, 190);

	QVBoxLayout *layout = new QVBoxLayout;
	setLayout(layout);
	layout->setSpacing(6);
	layout->setContentsMargins(12, 12, 12, 12);

	QHBoxLayout *nameRowLayout = new QHBoxLayout;
	nameLabel = new QLabel("标记名称:", this);
	nameLabel->setFixedHeight(20);
	nameLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
	nameRowLayout->addWidget(nameLabel);

	nameCombo = new QComboBox(this);
	nameCombo->setFixedHeight(28);
	nameCombo->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
	nameRowLayout->addWidget(nameCombo, 1);

	layout->addLayout(nameRowLayout);

	commentLabel = new QLabel("注释:", this);
	commentLabel->setFixedHeight(20);
	commentLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
	layout->addWidget(commentLabel);

	commentInput = new QTextEdit(this);
	commentInput->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
	commentInput->setMinimumHeight(60);
	commentInput->setMaximumHeight(600);
	commentInput->installEventFilter(this);
	layout->addWidget(commentInput, 1);

	QDialogButtonBox *buttonbox = new QDialogButtonBox(
		QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	buttonbox->setFixedHeight(40);
	buttonbox->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
	layout->addWidget(buttonbox);
	buttonbox->setCenterButtons(true);
	connect(buttonbox, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttonbox, &QDialogButtonBox::rejected, this, &QDialog::reject);
	
	// 【改进】使用原子标志
	s_isDialogOpen.store(true);
	loadWindowState();
	
	// 延迟设置焦点，确保对话框完全初始化并显示
	QTimer::singleShot(100, this, [this]() {
#ifdef _WIN32
		// 在全屏游戏时强制将窗口带到前台
		HWND hwnd = reinterpret_cast<HWND>(winId());
		if (hwnd) {
			// 允许当前线程设置前台窗口
			AllowSetForegroundWindow(ASFW_ANY);
			// 强制将窗口带到前台
			SetForegroundWindow(hwnd);
			SetActiveWindow(hwnd);
			
			// 获取注释输入框的窗口句柄并模拟鼠标点击
			HWND commentHwnd = reinterpret_cast<HWND>(commentInput->winId());
			if (commentHwnd) {
				// 获取输入框的中心位置
				RECT rect;
				GetWindowRect(commentHwnd, &rect);
				int x = (rect.left + rect.right) / 2;
				int y = (rect.top + rect.bottom) / 2;
				
				// 模拟鼠标点击
				mouse_event(MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE, x * 65535 / GetSystemMetrics(SM_CXSCREEN), y * 65535 / GetSystemMetrics(SM_CYSCREEN), 0, 0);
				mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
				mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
			}
		}
#endif
		commentInput->setFocus(Qt::PopupFocusReason);
		commentInput->activateWindow();
	});
}

ChapterWithCommentDialog::~ChapterWithCommentDialog()
{
	saveWindowState();
	s_isDialogOpen.store(false);
}

void ChapterWithCommentDialog::closeEvent(QCloseEvent *event)
{
	saveWindowState();
	QDialog::closeEvent(event);
}

bool ChapterWithCommentDialog::eventFilter(QObject *obj, QEvent *event)
{
	if (obj == commentInput && event->type() == QEvent::KeyPress) {
		QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
		if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
			if (keyEvent->modifiers() & Qt::ShiftModifier) {
				commentInput->textCursor().insertText("\n");
				return true;
			} else {
				accept();
				return true;
			}
		}
	}
	return QDialog::eventFilter(obj, event);
}

void ChapterWithCommentDialog::saveWindowState()
{
	QString configPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/chapter-hotkeys/window-state.json";
	QDir configDir(QFileInfo(configPath).absolutePath());
	if (!configDir.exists()) {
		configDir.mkpath(".");
	}

	QJsonObject windowState;
	windowState["x"] = x();
	windowState["y"] = y();
	windowState["height"] = height();
	if (screen()) {
		windowState["screenName"] = screen()->name();
	}

	QJsonObject root;
	root["commentDialog"] = windowState;

	QFile file(configPath);
	if (file.open(QIODevice::WriteOnly)) {
		file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
		file.close();
	}
}

void ChapterWithCommentDialog::loadWindowState()
{
	QString configPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/chapter-hotkeys/window-state.json";
	QFile file(configPath);
	if (!file.open(QIODevice::ReadOnly)) {
		return;
	}

	QByteArray data = file.readAll();
	file.close();

	QJsonDocument doc = QJsonDocument::fromJson(data);
	if (!doc.isObject()) {
		return;
	}

	QJsonObject root = doc.object();
	if (!root.contains("commentDialog")) {
		return;
	}

	QJsonObject windowState = root["commentDialog"].toObject();
	int x = windowState.value("x").toInt(INT_MIN);
	int y = windowState.value("y").toInt(INT_MIN);
	int height = windowState.value("height").toInt(190);
	QString screenName = windowState.value("screenName").toString();

	// 找到对应的屏幕
	QScreen *targetScreen = nullptr;
	if (!screenName.isEmpty()) {
		for (QScreen *s : QGuiApplication::screens()) {
			if (s->name() == screenName) {
				targetScreen = s;
				break;
			}
		}
	}

	// 如果没有找到目标屏幕，使用主屏幕
	if (!targetScreen) {
		targetScreen = QGuiApplication::primaryScreen();
	}

	// 恢复位置和尺寸
	if (targetScreen) {
		QRect screenGeometry = targetScreen->availableGeometry();
		
		// 设置最小高度和最大高度
		int finalHeight = qBound(190, height, 600);
		
		// 确保窗口在屏幕范围内
		if (x == INT_MIN || y == INT_MIN) {
			// 第一次打开，居中显示
			move(screenGeometry.center() - QPoint(140, finalHeight / 2));
		} else {
			// 恢复到之前的位置
			move(x, y);
		}
		
		// 设置高度
		resize(280, finalHeight);
	}
}

static bool IsWhitespace(char ch)
{
	return ch == ' ' || ch == '\t';
}

static void CleanWhitespace(std::string &str)
{
	while (str.size() && IsWhitespace(str.back()))
		str.erase(str.end() - 1);
	while (str.size() && IsWhitespace(str.front()))
		str.erase(str.begin());
}

bool ChapterWithCommentDialog::AskForNameAndComment(QWidget *parent, const QString &title,
						   const QString &text, std::string &nameInput,
						   std::string &commentInput,
						   const QString &placeHolder,
						   QString *colorInput)
{
	// 【改进】使用互斥锁防止多线程同时创建对话框
	QMutexLocker locker(&s_dialogMutex);
	
	// 再次检查标志（双重检查锁定）
	if (s_isDialogOpen.load()) {
		plog(LOG_WARNING, "AskForNameAndComment: dialog already open");
		return false;
	}
	
	ChapterWithCommentDialog dialog(parent);
	dialog.setWindowTitle(title);
	
	// 添加带颜色圆点的标记名称到下拉框
	if (hk_edit) {
		for (int i = 0; i < hk_edit->ui->listWidget->count(); i++) {
			auto item = hk_edit->ui->listWidget->item(i);
			QString name = item->data(Name).toString();
			QString color = item->data(Color).toString();
			if (!name.isEmpty()) {
				// 创建彩色圆点图标
				QIcon icon;
				if (!color.isEmpty() && color != "none") {
					QColor circleColor = getColorFromHex(color);
					int diameter = 12;
					QPixmap pixmap(diameter, diameter);
					pixmap.fill(Qt::transparent);
					QPainter painter(&pixmap);
					painter.setRenderHint(QPainter::Antialiasing);
					painter.setBrush(circleColor);
					painter.setPen(QPen(QColor("#333333"), 1));
					painter.drawEllipse(0, 0, diameter - 1, diameter - 1);
					painter.end();
					icon = QIcon(pixmap);
				}
				dialog.nameCombo->addItem(icon, name, name); // 设置原始名称为项数据
				dialog.nameCombo->setItemData(dialog.nameCombo->count() - 1, color, Qt::UserRole + 1); // 存储颜色
			}
		}
	}
	
	// 查找匹配的占位符（原始名称）
	int index = -1;
	for (int i = 0; i < dialog.nameCombo->count(); i++) {
		if (dialog.nameCombo->itemData(i).toString() == placeHolder) {
			index = i;
			break;
		}
	}
	if (index >= 0) {
		dialog.nameCombo->setCurrentIndex(index);
	}
	
	dialog.raise();

	// 居中对话框
	dialog.adjustSize(); // 确保布局完成
	if (parent) {
		QScreen *screen = parent->screen();
		if (screen) {
			QRect screenGeometry = screen->geometry();
			dialog.move(screenGeometry.center() - QPoint(dialog.width() / 2, dialog.height() / 2));
		}
	} else {
		// 如果没有父窗口，使用主屏幕
		QScreen *screen = QGuiApplication::primaryScreen();
		if (screen) {
			QRect screenGeometry = screen->geometry();
			dialog.move(screenGeometry.center() - QPoint(dialog.width() / 2, dialog.height() / 2));
		}
	}

	if (dialog.exec() != QDialog::Accepted) {
		return false;
	}

	// 获取原始名称（从项数据），如果项数据为空则使用显示文本
	QString selectedName = dialog.nameCombo->currentData().toString();
	// 获取选中的颜色
	if (colorInput) {
		*colorInput = dialog.nameCombo->currentData(Qt::UserRole + 1).toString();
	}
	
	if (selectedName.isEmpty()) {
		selectedName = dialog.nameCombo->currentText();
		// 尝试移除颜色前缀
		QRegularExpression colorPattern("\\((?<color>green|red|purple|orange|yellow|white|blue|cyan)\\)\\s*");
		selectedName.remove(colorPattern);
	}
	nameInput = selectedName.toUtf8().constData();
	commentInput = dialog.commentInput->toPlainText().toUtf8().constData();
	CleanWhitespace(nameInput);
	CleanWhitespace(commentInput);
	return true;
}

// ============================================================================
// ChapterNameDialog
// ============================================================================
ChapterNameDialog::ChapterNameDialog(QWidget *parent) : QDialog(parent)
{
	setModal(true);
	setWindowModality(Qt::WindowModality::WindowModal);
	setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint | Qt::WindowStaysOnTopHint);
	setFixedWidth(400);
	setMinimumHeight(100);

	QVBoxLayout *layout = new QVBoxLayout;
	setLayout(layout);

	label = new QLabel(this);
	layout->addWidget(label);

	input = new QLineEdit(this);
	layout->addWidget(input);

	QDialogButtonBox *buttonbox = new QDialogButtonBox(
		QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	layout->addWidget(buttonbox);
	buttonbox->setCenterButtons(true);
	connect(buttonbox, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttonbox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

bool ChapterNameDialog::AskForName(QWidget *parent, const QString &title,
				   const QString &text, std::string &name,
				   const QString &placeHolder)
{
	ChapterNameDialog dialog(parent);
	dialog.setWindowTitle(title);
	dialog.label->setText(text);
	dialog.input->setText(placeHolder);
	dialog.input->selectAll();

	// 居中对话框
	dialog.adjustSize(); // 确保布局完成
	if (parent) {
		QScreen *screen = parent->screen();
		if (screen) {
			QRect screenGeometry = screen->geometry();
			dialog.move(screenGeometry.center() - QPoint(dialog.width() / 2, dialog.height() / 2));
		}
	} else {
		// 如果没有父窗口，使用主屏幕
		QScreen *screen = QGuiApplication::primaryScreen();
		if (screen) {
			QRect screenGeometry = screen->geometry();
			dialog.move(screenGeometry.center() - QPoint(dialog.width() / 2, dialog.height() / 2));
		}
	}

	if (dialog.exec() != QDialog::Accepted)
		return false;

	name = dialog.input->text().toUtf8().constData();
	CleanWhitespace(name);
	return true;
}

// ============================================================================
// MarkerLivePanel - 实时标记预览面板 (OBS Dock Widget)
// 注意: 继承 QWidget 而非 QDockWidget，由 obs_frontend_add_dock_by_id 自动包裹为 Dock
// ============================================================================
MarkerLivePanel::MarkerLivePanel(QWidget *parent)
	: QWidget(parent)
{
	setObjectName("MarkerLivePanel");
	
	QVBoxLayout *mainLayout = new QVBoxLayout(this);
	mainLayout->setSpacing(4);
	mainLayout->setContentsMargins(6, 6, 6, 6);
	
	// 状态栏
	QHBoxLayout *statusLayout = new QHBoxLayout;
	statusLabel = new QLabel("⏹ 未录制", this);
	statusLabel->setStyleSheet("font-weight: bold; color: #999;");
	statusLayout->addWidget(statusLabel);
	
	statusLayout->addStretch();
	
	timerLabel = new QLabel("00:00:00", this);
	timerLabel->setStyleSheet("font-family: monospace; font-size: 14px; font-weight: bold; color: #ccc;");
	statusLayout->addWidget(timerLabel);
	mainLayout->addLayout(statusLayout);
	
	// 标记列表
	markerList = new QListWidget(this);
	markerList->setStyleSheet(
		"QListWidget { background: #1e1e2e; border: 1px solid #333; border-radius: 4px; }"
		"QListWidget::item { padding: 4px 8px; border-bottom: 1px solid #2a2a3e; color: #e0e0e0; }"
		"QListWidget::item:selected { background: #3a3a5e; }"
	);
	markerList->setAlternatingRowColors(false);
	mainLayout->addWidget(markerList, 1);
	
	// 底部按钮
	QHBoxLayout *btnLayout = new QHBoxLayout;
	
	clearBtn = new QPushButton("🗑 清空", this);
	clearBtn->setToolTip("清空标记列表");
	btnLayout->addWidget(clearBtn);
	
	copyBtn = new QPushButton("📋 复制摘要", this);
	copyBtn->setToolTip("复制标记摘要到剪贴板");
	btnLayout->addWidget(copyBtn);
	
	mainLayout->addLayout(btnLayout);
	
	// 定时器 - 更新录制时间
	recordingTimer = new QTimer(this);
	recordingTimer->setInterval(500);
	connect(recordingTimer, &QTimer::timeout, this, &MarkerLivePanel::updateRecordingTime);
	
	// 按钮连接
	connect(clearBtn, &QPushButton::clicked, this, &MarkerLivePanel::clearMarkers);
	connect(copyBtn, &QPushButton::clicked, this, &MarkerLivePanel::copyMarkersToClipboard);
	
	// 监听OBS录制事件
	auto recordingStartedCb = [](enum obs_frontend_event event, void *data) {
		auto panel = static_cast<MarkerLivePanel *>(data);
		if (event == OBS_FRONTEND_EVENT_RECORDING_STARTED) {
			QMetaObject::invokeMethod(panel, &MarkerLivePanel::onRecordingStarted, Qt::QueuedConnection);
		} else if (event == OBS_FRONTEND_EVENT_RECORDING_STOPPED) {
			QMetaObject::invokeMethod(panel, &MarkerLivePanel::onRecordingStopped, Qt::QueuedConnection);
		}
	};
	obs_frontend_add_event_callback(recordingStartedCb, this);
	
	// 如果已经在录制
	if (obs_frontend_recording_active()) {
		onRecordingStarted();
	}
}

MarkerLivePanel::~MarkerLivePanel()
{
	if (recordingTimer->isActive()) {
		recordingTimer->stop();
	}
}

void MarkerLivePanel::onRecordingStarted()
{
	isRecording = true;
	recordingStartTime = QDateTime::currentMSecsSinceEpoch();
	statusLabel->setText("🔴 录制中");
	statusLabel->setStyleSheet("font-weight: bold; color: #ff4444;");
	clearMarkers();
	recordingTimer->start();
}

void MarkerLivePanel::onRecordingStopped()
{
	isRecording = false;
	recordingTimer->stop();
	statusLabel->setText("⏹ 录制已停止");
	statusLabel->setStyleSheet("font-weight: bold; color: #999;");
	plog(LOG_INFO, "Recording stopped, total markers: %d", markers.size());
}

void MarkerLivePanel::updateRecordingTime()
{
	if (!isRecording) return;
	uint64_t elapsed = QDateTime::currentMSecsSinceEpoch() - recordingStartTime;
	timerLabel->setText(formatTime(elapsed));
}

void MarkerLivePanel::addMarker(const QString &name, const QString &color, const QString &comment)
{
	uint64_t elapsed = 0;
	if (isRecording && recordingStartTime > 0) {
		elapsed = QDateTime::currentMSecsSinceEpoch() - recordingStartTime;
	}
	
	LiveMarkerEntry entry;
	entry.index = markers.size() + 1;
	entry.name = name;
	entry.color = color;
	entry.comment = comment;
	entry.timestampMs = elapsed;
	entry.timeCode = formatTime(elapsed);
	
	markers.append(entry);
	
	// 添加到列表
	QString displayText = QString("#%1  [%2]  %3")
		.arg(entry.index, 2, 10, QChar('0'))
		.arg(entry.timeCode)
		.arg(name);
	if (!comment.isEmpty()) {
		displayText += "  💬 " + comment;
	}
	
	QListWidgetItem *item = new QListWidgetItem(displayText);
	
	// 设置颜色图标
	if (!color.isEmpty() && color != "none") {
		QColor circleColor = getColorFromHex(color);
		int diameter = 12;
		QPixmap pixmap(diameter, diameter);
		pixmap.fill(Qt::transparent);
		QPainter painter(&pixmap);
		painter.setRenderHint(QPainter::Antialiasing);
		painter.setBrush(circleColor);
		painter.setPen(QPen(QColor("#555555"), 1));
		painter.drawEllipse(0, 0, diameter - 1, diameter - 1);
		painter.end();
		item->setIcon(QIcon(pixmap));
	}
	
	markerList->addItem(item);
	markerList->scrollToBottom();
	
	plog(LOG_INFO, "Live panel: marker #%d added - %s at %s",
		entry.index, qUtf8Printable(name), qUtf8Printable(entry.timeCode));
}

void MarkerLivePanel::clearMarkers()
{
	markers.clear();
	markerList->clear();
}

QString MarkerLivePanel::formatTime(uint64_t ms) const
{
	int totalSeconds = ms / 1000;
	int hours = totalSeconds / 3600;
	int minutes = (totalSeconds % 3600) / 60;
	int seconds = totalSeconds % 60;
	return QString("%1:%2:%3")
		.arg(hours, 2, 10, QChar('0'))
		.arg(minutes, 2, 10, QChar('0'))
		.arg(seconds, 2, 10, QChar('0'));
}

void MarkerLivePanel::copyMarkersToClipboard()
{
	if (markers.isEmpty()) {
		return;
	}
	
	QString text;
	text += "# 标记摘要\n\n";
	text += "| # | 时间码 | 名称 | 颜色 | 注释 |\n";
	text += "|---|--------|------|------|------|\n";
	
	for (const auto &m : markers) {
		text += QString("| %1 | %2 | %3 | %4 | %5 |\n")
			.arg(m.index)
			.arg(m.timeCode)
			.arg(m.name)
			.arg(getColorName(m.color))
			.arg(m.comment);
	}
	
	QClipboard *clipboard = QApplication::clipboard();
	if (clipboard) {
		clipboard->setText(text);
	}
}

// ============================================================================
// 插件初始化和保存回调
// ============================================================================
static void LoadSaveHotkeys(obs_data_t *save_data, bool saving, void *)
{
	if (saving) {
		plog(LOG_INFO, "Saving hotkeys to OBS data...");
		OBSDataAutoRelease obj = obs_data_create();
		hk_edit->SaveHotkeys(obj);
		obs_data_set_obj(save_data, "chapter_hotkeys", obj);
		obs_data_set_bool(save_data, "enable_comments", g_enableComments);
		obs_data_set_string(save_data, "current_profile", 
			hk_edit->getCurrentProfileName().toUtf8().constData());
		plog(LOG_INFO, "Hotkeys saved, enableComments=%d, profile=%s", 
			g_enableComments, qUtf8Printable(hk_edit->getCurrentProfileName()));
	} else {
		plog(LOG_INFO, "Loading hotkeys from OBS data...");
		
		// 先执行旧版配置文件迁移
		hk_edit->migrateOldConfig();
		
		// 恢复上次选择的方案名
		const char *savedProfile = obs_data_get_string(save_data, "current_profile");
		if (savedProfile && *savedProfile) {
			// 将在 restoreProfileSelection 中使用
			hk_edit->setCurrentProfileName(QString::fromUtf8(savedProfile));
		}
		
		OBSDataAutoRelease obj =
			obs_data_get_obj(save_data, "chapter_hotkeys");
		if (obj) {
			hk_edit->LoadHotkeys(obj);
			plog(LOG_INFO, "Hotkeys loaded from OBS internal config");
			// 恢复方案选择状态（默认"预设"）
			hk_edit->restoreProfileSelection();
		} else {
			// 如果OBS内部配置不存在，从默认预设方案加载
			plog(LOG_INFO, "OBS internal config not found, loading from default preset");
			hk_edit->loadFromExternalConfig();
		}
		g_enableComments = obs_data_get_bool(save_data, "enable_comments");
		if (hk_edit->enableCommentsCheckBox) {
			hk_edit->enableCommentsCheckBox->setChecked(g_enableComments);
		}
		plog(LOG_INFO, "Hotkeys load complete, enableComments=%d", g_enableComments);
	}
}

extern "C" void InitChapterHotkeys()
{
	auto action =
		static_cast<QAction *>(obs_frontend_add_tools_menu_qaction(
			"章节标记热键"));
	auto window =
		static_cast<QMainWindow *>(obs_frontend_get_main_window());

	obs_frontend_push_ui_translation(obs_module_get_string);
	hk_edit = new ChapterHotkeyUI(window);
	obs_frontend_pop_ui_translation();

	obs_frontend_add_save_callback(LoadSaveHotkeys, nullptr);

	QAction::connect(action, &QAction::triggered, hk_edit,
			 &ChapterHotkeyUI::ShowHideDialog);
	
	// 创建实时标记预览面板 (OBS Dock)
	g_livePanel = new MarkerLivePanel(window);
	obs_frontend_add_dock_by_id("MarkerLivePanel", "标记实时预览", g_livePanel);
}

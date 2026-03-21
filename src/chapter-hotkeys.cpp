#include "chapter-hotkeys.hpp"
#include "plugin-logger.h"

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
		// === 配置方案区域 ===
		QGroupBox *profileGroup = new QGroupBox("配置方案", this);
		QHBoxLayout *profileLayout = new QHBoxLayout;
		profileLayout->setSpacing(4);
		profileLayout->setContentsMargins(4, 2, 4, 2);
		
		profileCombo = new QComboBox(this);
		profileCombo->setMinimumWidth(120);
		profileCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		profileLayout->addWidget(profileCombo);
		
		saveProfileBtn = new QPushButton(this);
		saveProfileBtn->setToolTip("新建方案");
		saveProfileBtn->setFixedSize(28, 28);
		saveProfileBtn->setIcon(QIcon(":/res/images/plus.svg"));
		saveProfileBtn->setIconSize(QSize(16, 16));
		profileLayout->addWidget(saveProfileBtn);
		
		deleteProfileBtn = new QPushButton(this);
		deleteProfileBtn->setToolTip("删除选中方案");
		deleteProfileBtn->setFixedSize(28, 28);
		deleteProfileBtn->setIcon(QIcon(":/res/images/minus.svg"));
		deleteProfileBtn->setIconSize(QSize(16, 16));
		profileLayout->addWidget(deleteProfileBtn);
		
		renameProfileBtn = new QPushButton(this);
		renameProfileBtn->setToolTip("重命名方案");
		renameProfileBtn->setFixedSize(28, 28);
		renameProfileBtn->setIcon(QIcon(":/res/images/cogs.svg"));
		renameProfileBtn->setIconSize(QSize(16, 16));
		profileLayout->addWidget(renameProfileBtn);
		
		profileGroup->setLayout(profileLayout);
		mainLayout->insertWidget(0, profileGroup);
		
		// === 启用注释复选框 ===
		enableCommentsCheckBox = new QCheckBox("启用标记注释", this);
		enableCommentsCheckBox->setChecked(false);
		connect(enableCommentsCheckBox, &QCheckBox::toggled, [](bool checked) {
			g_enableComments = checked;
		});
		mainLayout->insertWidget(1, enableCommentsCheckBox);
		
		// === 导入/导出按钮 ===
		QHBoxLayout *ioLayout = new QHBoxLayout;
		ioLayout->setSpacing(4);
		
		exportBtn = new QPushButton("📤 导出配置", this);
		exportBtn->setToolTip("导出标记配置到文件");
		ioLayout->addWidget(exportBtn);
		
		importBtn = new QPushButton("📥 导入配置", this);
		importBtn->setToolTip("从文件导入标记配置");
		ioLayout->addWidget(importBtn);
		
		mainLayout->addLayout(ioLayout);
		
		// === 连接信号 ===
		connect(profileCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
			this, &ChapterHotkeyUI::onProfileComboChanged);
		connect(saveProfileBtn, &QPushButton::clicked, this, &ChapterHotkeyUI::onSaveProfileClicked);
		connect(deleteProfileBtn, &QPushButton::clicked, this, &ChapterHotkeyUI::onDeleteProfileClicked);
		connect(renameProfileBtn, &QPushButton::clicked, this, &ChapterHotkeyUI::onRenameProfileClicked);
		connect(exportBtn, &QPushButton::clicked, this, &ChapterHotkeyUI::onExportClicked);
		connect(importBtn, &QPushButton::clicked, this, &ChapterHotkeyUI::onImportClicked);
		
		// 标记列表变更时自动保存（覆盖重命名等场景）
		connect(ui->listWidget, &QListWidget::itemChanged, this, [this](QListWidgetItem *) {
			autoSaveCurrentProfile();
		});
		
		// 初始化方案下拉框
		refreshProfileCombo();
	}
	
	plog(LOG_INFO, "ChapterHotkeyUI initialized, window size: %dx%d", width(), height());
}

void ChapterHotkeyUI::ShowHideDialog()
{
	if (!isVisible()) {
		setVisible(true);
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
		
		saveToExternalConfig();
		autoSaveCurrentProfile();
	}
}

// ============================================================================
// 外部配置文件路径
// ============================================================================
QString ChapterHotkeyUI::getExternalConfigPath()
{
	// 使用CEP扩展目录下的VideoMarkerExtractor_Data
	QString cepPath = "C:\\Program Files (x86)\\Common Files\\Adobe\\CEP\\extensions\\VideoMarkerExtractor\\VideoMarkerExtractor_Data";
	QDir cepDataDir(cepPath);
	
	// 如果目录不存在则创建
	if (!cepDataDir.exists()) {
		bool created = cepDataDir.mkpath(".");
		plog(LOG_INFO, "Creating CEP config directory: %s, success: %d", qUtf8Printable(cepDataDir.path()), created);
	}
	
	QString configPath = cepDataDir.filePath("chapter-markers-config.json");
	plog(LOG_INFO, "External config path: %s", qUtf8Printable(configPath));
	return configPath;
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
	QJsonArray markersArray;
	
	for (int i = 0; i < ui->listWidget->count(); i++) {
		auto item = ui->listWidget->item(i);
		
		auto name = item->data(Name).toString();
		auto uuid = item->data(HotkeyId).toString();
		auto color = item->data(Color).toString();
		OBSDataArrayAutoRelease bindings =
			static_cast<obs_data_array_t *>(
				item->data(Bindings).value<void *>());
		
		// 解析快捷键
		QString hotkeyStr;
		if (bindings) {
			size_t count = obs_data_array_count(bindings);
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
						if (keyStr.startsWith("OBS_KEY_")) {
							keyStr = keyStr.mid(8);
						}
						parts << keyStr;
					}
					
					hotkeyStr = parts.join("+");
					obs_data_release(binding);
				}
			}
		}

		// 外部配置文件不需要绑定信息，PR插件只需要名称和颜色
		QJsonObject markerObj;
		markerObj["name"] = name;
		markerObj["uuid"] = uuid;
		markerObj["color"] = color;
		markerObj["hotkey"] = hotkeyStr;
		
		markersArray.append(markerObj);
	}
	
	QJsonObject configObj;
	configObj["version"] = "2.0";
	configObj["profile"] = currentProfileName;
	configObj["enableComments"] = g_enableComments;
	configObj["markers"] = markersArray;
	
	QJsonDocument doc(configObj);
	QFile configFile(getExternalConfigPath());
	if (configFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
		configFile.write(doc.toJson(QJsonDocument::Indented));
		configFile.close();
		plog(LOG_INFO, "External config saved successfully to: %s", qUtf8Printable(configFile.fileName()));
	} else {
		plog(LOG_ERROR, "Failed to open config file for writing: %s", qUtf8Printable(configFile.fileName()));
	}
}

void ChapterHotkeyUI::loadFromExternalConfig()
{
	QFile configFile(getExternalConfigPath());
	if (!configFile.exists()) {
		return;
	}
	
	if (!configFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
		return;
	}
	
	QByteArray data = configFile.readAll();
	configFile.close();
	
	QJsonDocument doc = QJsonDocument::fromJson(data);
	if (doc.isNull() || !doc.isObject()) {
		return;
	}
	
	QJsonObject configObj = doc.object();
	QString version = configObj["version"].toString();
	if (version != "1.0" && version != "2.0") {
		return;
	}
	
	// 读取方案名
	if (configObj.contains("profile")) {
		currentProfileName = configObj["profile"].toString();
	}
	
	QJsonArray markersArray = configObj["markers"].toArray();
	
	ui->listWidget->clear();
	
	for (int i = 0; i < markersArray.size(); i++) {
		QJsonObject markerObj = markersArray[i].toObject();
		QString name = markerObj["name"].toString();
		QString uuid = markerObj["uuid"].toString();
		QString color = markerObj["color"].toString();
		QString hotkeyStr = markerObj["hotkey"].toString();
		
		if (name.isEmpty() || uuid.isEmpty()) continue;
		
		// 创建绑定数组
		OBSDataArrayAutoRelease bindings = nullptr;
		if (!hotkeyStr.isEmpty()) {
			bindings = obs_data_array_create();
			
			OBSDataAutoRelease binding = obs_data_create();
			
			// 解析快捷键字符串
			QStringList parts = hotkeyStr.split("+");
			QString keyStr;
			bool shift = false, control = false, alt = false, command = false;
			
			for (const QString &part : parts) {
				QString p = part.trimmed().toUpper();
				if (p == "CTRL" || p == "CONTROL") {
					control = true;
				} else if (p == "SHIFT") {
					shift = true;
				} else if (p == "ALT") {
					alt = true;
				} else if (p == "CMD" || p == "COMMAND") {
					command = true;
				} else {
					// 键名
					if (p.startsWith("NUM")) {
						keyStr = "OBS_KEY_NUMPAD" + p.mid(3);
					} else if (p.length() == 1) {
						keyStr = "OBS_KEY_" + p;
					} else {
						keyStr = "OBS_KEY_" + p;
					}
				}
			}
			
			obs_data_set_string(binding, "key", keyStr.toUtf8().constData());
			obs_data_set_bool(binding, "shift", shift);
			obs_data_set_bool(binding, "control", control);
			obs_data_set_bool(binding, "alt", alt);
			obs_data_set_bool(binding, "command", command);
			
			obs_data_array_push_back(bindings, binding);
		}
		
		auto hkItem = new ChapterHotkeyItem(uuid, name.toUtf8().constData(), bindings, 
			color.isEmpty() ? "#718637" : color);
		ui->listWidget->addItem(hkItem);
	}
	
	ui->listWidget->sortItems();
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
	saveToExternalConfig();
	
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
	profileCombo->addItem("-- 选择方案 --");
	
	QStringList profiles = getProfileNames();
	for (const QString &p : profiles) {
		profileCombo->addItem(p);
	}
	
	// 选中当前方案
	if (!currentProfileName.isEmpty()) {
		int idx = profileCombo->findText(currentProfileName);
		if (idx >= 0) profileCombo->setCurrentIndex(idx);
	}
	
	profileCombo->blockSignals(false);
}

void ChapterHotkeyUI::autoSaveCurrentProfile()
{
	// 如果当前没有选中方案，则不自动保存
	if (currentProfileName.isEmpty()) return;
	
	saveCurrentAsProfile(currentProfileName);
	saveToExternalConfig();
	plog(LOG_INFO, "Auto-saved to profile: %s (%d markers)",
		qUtf8Printable(currentProfileName), ui->listWidget->count());
}

void ChapterHotkeyUI::onProfileComboChanged(int index)
{
	if (index <= 0) return; // "-- 选择方案 --"
	
	QString profileName = profileCombo->currentText();
	if (profileName.isEmpty() || profileName == "-- 选择方案 --") return;
	
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
	if (profileCombo->currentIndex() <= 0) {
		QMessageBox::warning(this, "提示", "请先选择要删除的方案。");
		return;
	}
	
	QString name = profileCombo->currentText();
	auto ret = QMessageBox::question(this, "删除方案",
		QString("确定要删除方案「%1」吗？此操作不可撤销。").arg(name),
		QMessageBox::Yes | QMessageBox::No);
	
	if (ret == QMessageBox::Yes) {
		deleteProfile(name);
		refreshProfileCombo();
	}
}

void ChapterHotkeyUI::onRenameProfileClicked()
{
	if (profileCombo->currentIndex() <= 0) {
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
		saveToExternalConfig();
		plog(LOG_INFO, "Profile renamed: %s -> %s", qUtf8Printable(oldName), qUtf8Printable(newName));
	} else {
		QMessageBox::warning(this, "重命名失败", "文件重命名失败，请检查文件权限。");
	}
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
	
	saveToExternalConfig();
	refreshProfileCombo();
	
	QMessageBox::information(this, "导入成功",
		QString("已%1 %2 个标记。").arg(replace ? "导入" : "合并").arg(imported));
}

void ChapterHotkeyUI::onExportClicked()
{
	QString defaultName = "chapter-markers-config";
	if (!currentProfileName.isEmpty()) {
		defaultName = currentProfileName;
	}
	
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
	
	// 同时保存到外部配置文件供PR插件读取
	saveToExternalConfig();
	
	// 同时保存到当前方案文件（确保 OBS 设置中修改的快捷键绑定也保存到方案）
	if (!currentProfileName.isEmpty()) {
		saveCurrentAsProfile(currentProfileName);
		plog(LOG_INFO, "Profile also saved during OBS save: %s", qUtf8Printable(currentProfileName));
	}
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
	if (hotkeyText.isEmpty() || hotkeyText == "None") {
		setText(QString::fromStdString(chapterName));
	} else {
		// 名称后面用空格和快捷键文本，格式如 "bug        [NUM1]"
		setText(QString::fromStdString(chapterName) + "    [" + hotkeyText + "]");
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
		plog(LOG_INFO, "Hotkeys saved, enableComments=%d", g_enableComments);
	} else {
		plog(LOG_INFO, "Loading hotkeys from OBS data...");
		OBSDataAutoRelease obj =
			obs_data_get_obj(save_data, "chapter_hotkeys");
		if (obj) {
			hk_edit->LoadHotkeys(obj);
			plog(LOG_INFO, "Hotkeys loaded from OBS internal config");
		} else {
			// 如果OBS内部配置不存在，尝试从外部配置文件加载
			plog(LOG_INFO, "OBS internal config not found, loading from external config file");
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

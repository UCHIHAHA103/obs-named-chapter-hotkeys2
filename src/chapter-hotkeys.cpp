#include "chapter-hotkeys.hpp"

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
#include <QMessageBox>
#include <QTimer>
#include <QTextEdit>

using namespace std;

bool ChapterWithCommentDialog::s_isDialogOpen = false;

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
bool g_enableComments = false;

ChapterHotkeyUI::ChapterHotkeyUI(QWidget *parent)
	: QDialog(parent),
	  ui(new Ui_HotkeyChaptersDialog)
{
	ui->setupUi(this);
	setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
	setMinimumWidth(300);
	resize(300, 410);
	ui->listWidget->setSortingEnabled(true);

	QVBoxLayout *mainLayout = qobject_cast<QVBoxLayout *>(layout());
	if (mainLayout) {
		enableCommentsCheckBox = new QCheckBox("启用标记注释", this);
		enableCommentsCheckBox->setChecked(false);
		connect(enableCommentsCheckBox, &QCheckBox::toggled, [](bool checked) {
			g_enableComments = checked;
		});
		mainLayout->insertWidget(0, enableCommentsCheckBox);
	}
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
}

void ChapterHotkeyUI::on_actionRemoveHotkey_triggered()
{
	auto item = ui->listWidget->currentItem();
	delete item;
	ui->listWidget->sortItems();
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
	}
}

QString ChapterHotkeyUI::getExternalConfigPath()
{
	QString userProfile = qgetenv("USERPROFILE");
	if (userProfile.isEmpty()) {
		userProfile = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
		blog(LOG_WARNING, "USERPROFILE not found, using Documents: %s", qPrintable(userProfile));
	}
	QDir dataDir(userProfile + "/VideoMarkerExtractor_Data");
	if (!dataDir.exists()) {
		bool created = dataDir.mkpath(".");
		blog(LOG_INFO, "Creating config directory: %s, success: %d", qPrintable(dataDir.path()), created);
	}
	QString configPath = dataDir.filePath("chapter-markers-config.json");
	blog(LOG_INFO, "External config path: %s", qPrintable(configPath));
	return configPath;
}

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
	configObj["version"] = "1.0";
	configObj["markers"] = markersArray;
	
	QJsonDocument doc(configObj);
	QFile configFile(getExternalConfigPath());
	if (configFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
		configFile.write(doc.toJson(QJsonDocument::Indented));
		configFile.close();
		blog(LOG_INFO, "External config saved successfully to: %s", qPrintable(configFile.fileName()));
	} else {
		blog(LOG_ERROR, "Failed to open config file for writing: %s", qPrintable(configFile.fileName()));
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
	if (configObj["version"].toString() != "1.0") {
		return;
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
}

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
}

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
	setText(QString::fromStdString(chapterName));
	
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
	
	setToolTip(QString("Hotkey: %1").arg(getHotkeyText()));
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
	
	QStringList keys;
	for (size_t i = 0; i < count; i++) {
		obs_data_t *binding = obs_data_array_item(bindings, i);
		obs_data_t *key = obs_data_get_obj(binding, "key");
		if (key) {
			const char *keyText = obs_data_get_string(key, "text");
			if (keyText && strlen(keyText) > 0) {
				keys.append(QString::fromUtf8(keyText));
			}
			obs_data_release(key);
		}
		obs_data_release(binding);
	}
	obs_data_array_release(bindings);
	
	if (keys.isEmpty()) return "None";
	return keys.join(" + ");
}

static QString g_pendingChapterName;
static QString g_pendingColorHex;

static void ShowCommentDialog();

void ChapterHotkeyItem::HotkeyPressed(void *_this, obs_hotkey_id,
			      obs_hotkey_t *, bool pressed)
{
	auto hk = static_cast<ChapterHotkeyItem *>(_this);

	if (pressed) {
		// 检查OBS是否正在录制，只有在录制时才启用标记功能
		if (!obs_frontend_recording_active()) {
			return;
		}
		
		// 如果注释窗口已打开，不创建任何标记
		if (ChapterWithCommentDialog::IsDialogOpen()) {
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
		
		QTimer::singleShot(0, []() {
			ShowCommentDialog();
		});
	}
}

static void ShowCommentDialog()
{
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
	}
}

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

	// 延迟设置焦点，确保对话框完全初始化
	QTimer::singleShot(0, this, [this]() {
		commentInput->setFocus();
	});

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
	
	s_isDialogOpen = true;
	loadWindowState();
}

ChapterWithCommentDialog::~ChapterWithCommentDialog()
{
	saveWindowState();
	s_isDialogOpen = false;
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

static void LoadSaveHotkeys(obs_data_t *save_data, bool saving, void *)
{
	if (saving) {
		OBSDataAutoRelease obj = obs_data_create();
		hk_edit->SaveHotkeys(obj);
		obs_data_set_obj(save_data, "chapter_hotkeys", obj);
		obs_data_set_bool(save_data, "enable_comments", g_enableComments);
	} else {
		OBSDataAutoRelease obj =
			obs_data_get_obj(save_data, "chapter_hotkeys");
		if (obj) {
			hk_edit->LoadHotkeys(obj);
		} else {
			// 如果OBS内部配置不存在，尝试从外部配置文件加载
			hk_edit->loadFromExternalConfig();
		}
		g_enableComments = obs_data_get_bool(save_data, "enable_comments");
		if (hk_edit->enableCommentsCheckBox) {
			hk_edit->enableCommentsCheckBox->setChecked(g_enableComments);
		}
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
}

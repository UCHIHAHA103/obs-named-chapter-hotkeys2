#include "chapter-hotkeys.hpp"

#include <QAction>
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

using namespace std;

ChapterHotkeyUI *hk_edit;
bool g_enableComments = false;

ChapterHotkeyUI::ChapterHotkeyUI(QWidget *parent)
	: QDialog(parent),
	  ui(new Ui_HotkeyChaptersDialog)
{
	ui->setupUi(this);
	setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
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

	auto item = new ChapterHotkeyItem(id, name.c_str(), nullptr, "green");
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
	Qt::ItemFlags flags = item->flags();

	item->setFlags(flags | Qt::ItemIsEditable);
	ui->listWidget->editItem(item);
	item->setFlags(flags);
	ui->listWidget->sortItems();
}

void ChapterHotkeyUI::on_colorButtonGreen_clicked() { setSelectedItemColor("green"); }
void ChapterHotkeyUI::on_colorButtonRed_clicked() { setSelectedItemColor("red"); }
void ChapterHotkeyUI::on_colorButtonPurple_clicked() { setSelectedItemColor("purple"); }
void ChapterHotkeyUI::on_colorButtonOrange_clicked() { setSelectedItemColor("orange"); }
void ChapterHotkeyUI::on_colorButtonYellow_clicked() { setSelectedItemColor("yellow"); }
void ChapterHotkeyUI::on_colorButtonWhite_clicked() { setSelectedItemColor("white"); }
void ChapterHotkeyUI::on_colorButtonBlue_clicked() { setSelectedItemColor("blue"); }
void ChapterHotkeyUI::on_colorButtonCyan_clicked() { setSelectedItemColor("cyan"); }

void ChapterHotkeyUI::setSelectedItemColor(const QString &color)
{
	auto item = ui->listWidget->currentItem();
	if (item) {
		item->setData(Color, color);
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
		
		// 外部配置文件不需要绑定信息，PR插件只需要名称和颜色
		QJsonObject markerObj;
		markerObj["name"] = name;
		markerObj["uuid"] = uuid;
		markerObj["color"] = color;
		
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
	
	// 注意：这里不直接加载到UI，因为OBS配置是主配置源
	// 外部配置主要用于PR插件读取，OBS配置是权威源
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

		auto hkItem = new ChapterHotkeyItem(id, name, bindings, color ? color : "green");
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
		QColor circleColor(color);
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
}

void ChapterHotkeyItem::HotkeyPressed(void *_this, obs_hotkey_id,
				      obs_hotkey_t *, bool pressed)
{
	auto hk = static_cast<ChapterHotkeyItem *>(_this);

	if (pressed) {
		if (g_enableComments) {
			string nameInput = hk->chapterName;
			string commentInput;

			auto window = static_cast<QMainWindow *>(obs_frontend_get_main_window());
			bool accepted = ChapterWithCommentDialog::AskForNameAndComment(
				window, 
				"添加标记注释", 
				"请输入标记注释", 
				nameInput, 
				commentInput,
				QString::fromStdString(hk->chapterName));

			if (accepted) {
				string finalChapterName = nameInput;
				if (!commentInput.empty()) {
					finalChapterName = nameInput + "@" + commentInput;
				}
				// 添加颜色前缀
				if (!hk->color.isEmpty() && hk->color != "none") {
					finalChapterName = "(" + hk->color.toStdString() + ") " + finalChapterName;
				}
				obs_frontend_recording_add_chapter(finalChapterName.c_str());
			}
		} else {
			string finalChapterName = hk->chapterName;
			// 添加颜色前缀
			if (!hk->color.isEmpty() && hk->color != "none") {
				finalChapterName = "(" + hk->color.toStdString() + ") " + finalChapterName;
			}
			obs_frontend_recording_add_chapter(finalChapterName.c_str());
		}
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
	setFixedWidth(400);
	setMinimumHeight(200);

	QVBoxLayout *layout = new QVBoxLayout;
	setLayout(layout);

	label = new QLabel(this);
	layout->addWidget(label);
	label->setText("添加标记注释");

	nameLabel = new QLabel("标记名称:", this);
	layout->addWidget(nameLabel);

	nameCombo = new QComboBox(this);
	layout->addWidget(nameCombo);

	commentLabel = new QLabel("注释:", this);
	layout->addWidget(commentLabel);

	commentInput = new QLineEdit(this);
	layout->addWidget(commentInput);

	QDialogButtonBox *buttonbox = new QDialogButtonBox(
		QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	layout->addWidget(buttonbox);
	buttonbox->setCenterButtons(true);
	connect(buttonbox, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttonbox, &QDialogButtonBox::rejected, this, &QDialog::reject);
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
						   const QString &placeHolder)
{
	ChapterWithCommentDialog dialog(parent);
	dialog.setWindowTitle(title);

	dialog.label->setText(text);
	
	// 使用静态方法获取所有章节名称
	QStringList allNames = ChapterHotkeyUI::GetAllChapterNames();
	// GetAllChapterNames已经返回带颜色前缀的名称，但只返回display name
	// 我们需要获取原始名称作为数据，所以需要另一种方法
	if (hk_edit) {
		for (int i = 0; i < hk_edit->ui->listWidget->count(); i++) {
			auto item = hk_edit->ui->listWidget->item(i);
			QString name = item->data(Name).toString();
			QString color = item->data(Color).toString();
			if (!name.isEmpty()) {
				QString displayName = name;
				if (!color.isEmpty() && color != "none") {
					displayName = QString("(%1) %2").arg(color).arg(name);
				}
				dialog.nameCombo->addItem(displayName, name); // 设置原始名称为项数据
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
	
	dialog.commentInput->setFocus();

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
	if (selectedName.isEmpty()) {
		selectedName = dialog.nameCombo->currentText();
		// 尝试移除颜色前缀
		QRegularExpression colorPattern("\\((?<color>green|red|purple|orange|yellow|white|blue|cyan)\\)\\s*");
		selectedName.remove(colorPattern);
	}
	nameInput = selectedName.toUtf8().constData();
	commentInput = dialog.commentInput->text().toUtf8().constData();
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
		if (obj)
			hk_edit->LoadHotkeys(obj);
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

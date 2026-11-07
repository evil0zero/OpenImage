#include "engine.hpp"

#include <QApplication>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QInputDialog>
#include <QPromise>
#include <QMetaObject>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QStackedWidget>
#include <QStorageInfo>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrentRun>
#include <algorithm>
#include <optional>
#ifdef OPENIMAGE_CAPTURE_TEST
#include <QDir>
#include <QPixmap>
#endif

namespace {
enum Operation { Backup, Restore, Clone, Verify, Explore, Compare };

constexpr const char* operation_names[]{"Backup", "Restore", "Clone", "Verify", "Explore", "Compare"};
constexpr const char* operation_details[]{
    "Image a disk or file",
    "Recover a disk or file",
    "Copy a disk or file",
    "Check an image for errors",
    "Read an image header",
    "Compare an image with a file"
};

oi::fs::path native_path(const QString& text) {
#ifdef _WIN32
    return oi::fs::path(text.toStdWString());
#else
    return oi::fs::path(text.toUtf8().toStdString());
#endif
}

QString display_path(const oi::fs::path& path) {
#ifdef _WIN32
    return QString::fromStdWString(path.native());
#else
    return QString::fromStdString(path.native());
#endif
}

struct Inventory {
    std::vector<oi::Disk> disks;
    QList<QStorageInfo> volumes;
    QString error;
};

bool uses_target(int operation) {
    return operation != Verify && operation != Explore;
}

struct Result {
    QString message;
    bool ok;
};
}

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    QSettings settings("OpenImage", "OpenImage");
#ifdef OPENIMAGE_CAPTURE_TEST
    if (argc != 2) return 2;
#endif
    QWidget window;
    window.setWindowTitle("OpenImage | Disk Imaging and Recovery");
    window.resize(1040, 660);
    auto* layout = new QVBoxLayout(&window);
    auto* title = new QLabel("OpenImage  /  Disk Imaging and Recovery");
    title->setObjectName("title");
    layout->addWidget(title);
    auto* body = new QHBoxLayout;
    layout->addLayout(body, 1);
    auto* rail = new QWidget;
    rail->setObjectName("rail");
    auto* navigation = new QVBoxLayout(rail);
    body->addWidget(rail);
    auto* pages = new QStackedWidget;
    body->addWidget(pages, 1);

    auto* dashboard = new QWidget;
    auto* dashboard_layout = new QVBoxLayout(dashboard);
    auto* cards = new QGridLayout;
    dashboard_layout->addLayout(cards);
    auto* inventory_header = new QHBoxLayout;
    auto* inventory_title = new QLabel("Disk overview");
    inventory_title->setObjectName("section");
    auto* refresh = new QPushButton("Refresh");
    inventory_header->addWidget(inventory_title, 1);
    inventory_header->addWidget(refresh);
    dashboard_layout->addLayout(inventory_header);
    auto* table = new QTableWidget(0, 5);
    table->setHorizontalHeaderLabels({"Device", "Model", "Capacity", "Layout", "Status"});
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->verticalHeader()->hide();
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    dashboard_layout->addWidget(table, 1);
    auto* detail = new QLabel("Selected disk");
    detail->setObjectName("section");
    detail->setTextFormat(Qt::PlainText);
    dashboard_layout->addWidget(detail);
    auto* map = new QHBoxLayout;
    dashboard_layout->addLayout(map);
    auto* inventory_status = new QLabel("Discovering disks and mounted volumes…");
    inventory_status->setTextFormat(Qt::PlainText);
    inventory_status->setWordWrap(true);
    dashboard_layout->addWidget(inventory_status);
    pages->addWidget(dashboard);
    QFutureWatcher<Inventory> inventory;
    std::vector<oi::Disk> disks;
    const auto scan = [&] {
        if (inventory.isRunning()) return;
        refresh->setEnabled(false);
        inventory_status->setText("Discovering disks and mounted volumes…");
        inventory.setFuture(QtConcurrent::run([] {
            Inventory result;
            try {
                result.disks = oi::discover_disks();
            } catch (const std::exception& error) {
                result.error = QString::fromUtf8(error.what());
            }
            result.volumes = QStorageInfo::mountedVolumes();
            return result;
        }));
    };
    QObject::connect(refresh, &QPushButton::clicked, &window, scan);
    QObject::connect(table, &QTableWidget::currentCellChanged, &window, [&](int row) {
        while (auto* item = map->takeAt(0)) {
            delete item->widget();
            delete item;
        }
        if (row < 0 || static_cast<std::size_t>(row) >= disks.size()) {
            detail->setText("No disk selected");
            return;
        }
        const auto& disk = disks[row];
        detail->setText(QString("Partition layout  ·  %1  ·  %2  ·  Serial %3")
            .arg(display_path(disk.path), QString::fromStdString(disk.bus.empty() ? "Unknown bus" : disk.bus),
                 QString::fromStdString(disk.serial.empty() ? "unavailable" : disk.serial)));
        for (const auto& part : disk.partitions) {
            const auto name = part.mount.empty() ? QString::fromStdString(part.type) :
                display_path(part.mount) + "  " + QString::fromStdString(part.filesystem);
            auto* block = new QLabel(QString("%1\n%2").arg(name, QLocale().formattedDataSize(part.bytes)));
            block->setObjectName(part.type == "Windows Recovery" ? "recoveryPartition" :
                (part.type == "EFI System" ? "efiPartition" : "partition"));
            block->setAlignment(Qt::AlignCenter);
            block->setToolTip(QString("Partition %1 · Offset %2 bytes · Length %3 bytes")
                .arg(part.number).arg(part.offset).arg(part.bytes));
            const int weight = disk.bytes ? static_cast<int>(std::clamp(
                100.0 * static_cast<double>(part.bytes) / static_cast<double>(disk.bytes), 1.0, 100.0)) : 1;
            map->addWidget(block, weight);
        }
        if (disk.partitions.empty()) map->addWidget(new QLabel("No partition entries available."));
    });
    QTimer::singleShot(0, &window, scan);

    auto* workflow = new QWidget;
    auto* workflow_layout = new QVBoxLayout(workflow);
    auto* operation_title = new QLabel;
    operation_title->setObjectName("section");
    workflow_layout->addWidget(operation_title);
    auto* stage = new QLabel("1  Select     2  Configure     3  Review     4  Execute     5  Verify");
    stage->setObjectName("stage");
    workflow_layout->addWidget(stage);
    auto* form = new QFormLayout;
    workflow_layout->addLayout(form);
    pages->addWidget(workflow);
    int selected_operation = Backup;
    auto* source = new QLineEdit;
    auto* target = new QLineEdit;
    auto* source_devices = new QComboBox;
    auto* target_devices = new QComboBox;
    source_devices->addItem("Input file");
    target_devices->addItem("Destination file");
    auto add_path = [&](const QString& label, QLineEdit* edit, QComboBox* devices, bool secondary) {
        auto* row = new QHBoxLayout;
        auto* browse = new QPushButton("Browse…");
        row->addWidget(edit);
        row->addWidget(browse);
        row->addWidget(devices);
        QObject::connect(devices, &QComboBox::currentIndexChanged, &window, [=](int index) {
            edit->setVisible(index <= 0);
            browse->setVisible(index <= 0);
        });
        auto* caption = new QLabel(label);
        caption->setBuddy(edit);
        form->addRow(caption, row);
        QObject::connect(browse, &QPushButton::clicked, &window, [&, edit, secondary] {
            const bool new_file = secondary && selected_operation != Compare;
            const auto path = new_file ? QFileDialog::getSaveFileName(&window, "Choose new output") :
                QFileDialog::getOpenFileName(&window, "Choose input file");
            if (!path.isEmpty()) edit->setText(path);
        });
    };
    add_path("&Input", source, source_devices, false);
    add_path("&Destination", target, target_devices, true);
    QObject::connect(&inventory, &QFutureWatcher<Inventory>::finished, &window, [&] {
        auto result = inventory.result();
        table->setRowCount(0);
        disks = std::move(result.disks);
        source_devices->clear();
        target_devices->clear();
        source_devices->addItem("Input file");
        target_devices->addItem("Destination file");
        for (std::size_t index = 0; index < disks.size(); ++index) {
            const auto& disk = disks[index];
            const auto path = disk.path.empty() ? QString("Unavailable") : display_path(disk.path);
            const auto model = disk.model.empty() ? QString("Unknown model") : QString::fromStdString(disk.model).trimmed();
            const auto state = disk.attributes_known ? (disk.offline ? "Offline" : "Online") : "Unknown";
            const int row = table->rowCount();
            table->insertRow(row);
            const QString values[]{path, model,
                disk.bytes ? QLocale().formattedDataSize(disk.bytes) : QString("Unknown"),
                disk.layout.empty() ? QString("Unknown") : QString::fromStdString(disk.layout),
                QString(state) + (disk.read_only ? " · read-only" : "") +
                    (disk.running_os ? " · running OS" : "")};
            for (int column = 0; column < 5; ++column)
                table->setItem(row, column, new QTableWidgetItem(values[column]));
            if (disk.bytes && disk.sector && !disk.path.empty()) {
                const auto label = QString("%1 · %2 · %3").arg(path, model, state);
                source_devices->addItem(label, static_cast<int>(index));
                if (!disk.read_only && !disk.running_os) target_devices->addItem(label, static_cast<int>(index));
            }
        }
        if (!disks.empty()) table->setCurrentCell(0, 0);
        QStringList mounts;
        for (const auto& volume : result.volumes) {
            if (volume.isValid() && volume.isReady())
                mounts.append(QString("%1 (%2)").arg(volume.rootPath(), QString::fromUtf8(volume.fileSystemType())));
        }
        inventory_status->setText(result.error.isEmpty() ?
            QString("%1 disks discovered  ·  Mounted volumes %2").arg(table->rowCount())
                .arg(mounts.isEmpty() ? "unavailable" : mounts.join("  ·  ")) : result.error);
        refresh->setEnabled(true);
    });
    auto* codec = new QComboBox;
    codec->addItems({"Zstandard", "Raw", "LZ4", "LZMA2"});
    form->addRow("&Compression", codec);
    auto* preferences = new QWidget;
    auto* preferences_layout = new QFormLayout(preferences);
    auto* theme = new QComboBox;
    theme->addItems({"Professional", "LCARS"});
    preferences_layout->addRow("&Appearance", theme);
    pages->addWidget(preferences);

    auto* status = new QLabel("Choose an operation and review the paths before execution.");
    status->setTextFormat(Qt::PlainText);
    status->setWordWrap(true);
    auto* progress = new QProgressBar;
    auto* execute = new QPushButton("Review and run");
    execute->setObjectName("primary");
    workflow_layout->addWidget(execute, 0, Qt::AlignRight);
    workflow_layout->addStretch();
    layout->addWidget(new QLabel("Exact imaging  ·  Disk access requires administrator privileges and exclusive volume access"));
    layout->addWidget(progress);
    layout->addWidget(status);
    progress->hide();
    QFutureWatcher<Result> watcher;
    const auto select_operation = [&](int index) {
        if (index != selected_operation) {
            source->clear();
            target->clear();
        }
        selected_operation = index;
        operation_title->setText(QString::fromUtf8(operation_names[index]));
        source_devices->setCurrentIndex(0);
        target_devices->setCurrentIndex(0);
        source_devices->setVisible(index == Backup || index == Clone);
        target_devices->setVisible(index == Restore || index == Clone);
        const int selected_row = table->currentRow();
        if (selected_row >= 0) {
            if (index == Backup || index == Clone)
                source_devices->setCurrentIndex(std::max(0, source_devices->findData(selected_row)));
            if (index == Restore)
                target_devices->setCurrentIndex(std::max(0, target_devices->findData(selected_row)));
        }
        form->setRowVisible(1, uses_target(index));
        form->setRowVisible(codec, index == Backup);
        pages->setCurrentWidget(workflow);
    };
    const auto add_navigation = [&](const QString& name) {
        auto* button = new QPushButton(name);
        button->setObjectName("navigation");
        navigation->addWidget(button);
        return button;
    };
    QObject::connect(add_navigation("Home"), &QPushButton::clicked, &window,
                     [&] { pages->setCurrentWidget(dashboard); });
    QList<QPushButton*> operation_cards;
    for (int index = Backup; index <= Compare; ++index) {
        QObject::connect(add_navigation(operation_names[index]), &QPushButton::clicked, &window,
                         [&, index] { select_operation(index); });
        if (index == Compare) continue;
        auto* card = new QPushButton(QString("%1\n%2").arg(operation_names[index], operation_details[index]));
        card->setObjectName(index == Backup ? "primaryCard" :
                            (index == Clone || index == Explore ? "blueCard" : "card"));
        card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        operation_cards.append(card);
        QObject::connect(card, &QPushButton::clicked, &window, [&, index] { select_operation(index); });
    }
    QObject::connect(add_navigation("Settings"), &QPushButton::clicked, &window,
                     [&] { pages->setCurrentWidget(preferences); });
    navigation->addStretch();
    const auto apply_theme = [&](int index) {
#ifndef OPENIMAGE_CAPTURE_TEST
        settings.setValue("appearance", index);
#endif
        const bool lcars = index == 1;
        title->setText(lcars ? "OPENIMAGE   /   DISK IMAGING & RECOVERY" :
                              "OpenImage  /  Disk Imaging and Recovery");
        for (auto* card : operation_cards) cards->removeWidget(card);
        for (int card = 0; card < operation_cards.size(); ++card) {
            if (lcars) cards->addWidget(operation_cards[card], 0, card * 6, 1, 6);
            else if (card < 3) cards->addWidget(operation_cards[card], 0, card * 10, 1, 10);
            else cards->addWidget(operation_cards[card], 1, (card - 3) * 10 + 5, 1, 10);
        }
        window.setStyleSheet(lcars ?
            "QWidget{background:#08070b;color:#fff4df} QLabel#title{background:#ffad29;"
            "color:#181020;border-radius:22px;padding:15px;font-size:24px;font-weight:bold}"
            "QWidget#rail{border-left:10px solid #8d86e9;border-radius:19px}"
            "QPushButton,QComboBox{background:#332748;border:2px solid #a68aff;border-radius:15px;padding:7px}"
            "QPushButton#navigation{background:#9a94ed;color:#090614;font-weight:bold;text-align:left;padding-left:15px}"
            "QPushButton#card,QPushButton#blueCard,QPushButton#primaryCard{min-height:94px;font-weight:bold}"
            "QPushButton#blueCard{background:#15385e;border-color:#608cda}"
            "QPushButton#primary,QPushButton#primaryCard{background:#ffac27;color:#1e1200;border-color:#ffd36a;font-weight:bold}"
            "QLabel#section,QLabel#stage{color:#ffbb48;font-size:16px;font-weight:bold}"
            "QLabel#partition{background:#704cb0;border:1px solid #c3abff;border-radius:9px;padding:7px}"
            "QLabel#efiPartition{background:#12669c;border:1px solid #68cbfc;border-radius:9px;padding:7px}"
            "QLabel#recoveryPartition{background:#613393;border:1px solid #ad79de;border-radius:9px;padding:7px}"
            "QTableWidget,QLineEdit,QProgressBar{background:#15152a;border:1px solid #8b83ee;border-radius:8px;padding:5px}"
            "QHeaderView::section{background:#272340;color:#fff4df;padding:5px}"
            "QTableWidget::item:selected{background:#ffac27;color:#181020} QProgressBar::chunk{background:#ffad29}"
            "QPushButton:focus,QComboBox:focus,QLineEdit:focus{border:2px solid #fff4df}" :
            "QWidget{background:#0c1a2b;color:#e5efff} QLabel#title{background:#142d4a;"
            "border-radius:10px;padding:14px;font-size:24px;font-weight:bold}"
            "QWidget#rail{background:#101f33;border:1px solid #223b57;border-radius:9px}"
            "QPushButton,QComboBox{background:#19314d;border:1px solid #4c729a;border-radius:7px;padding:7px}"
            "QPushButton#navigation{text-align:left;padding-left:13px;min-width:95px}"
            "QPushButton#card,QPushButton#blueCard,QPushButton#primaryCard{min-height:51px;font-weight:bold}"
            "QPushButton#primary,QPushButton#primaryCard{background:#115ca5;border-color:#49aefa;font-weight:bold}"
            "QLabel#section,QLabel#stage{color:#b6d9ff;font-size:15px;font-weight:bold}"
            "QLabel#partition{background:#1c6b8d;border:1px solid #5ac6f2;border-radius:6px;padding:6px}"
            "QLabel#efiPartition{background:#1462a0;border:1px solid #5ac6f2;border-radius:6px;padding:6px}"
            "QLabel#recoveryPartition{background:#573785;border:1px solid #a386d3;border-radius:6px;padding:6px}"
            "QTableWidget,QLineEdit,QProgressBar{background:#10253b;border:1px solid #3d638b;border-radius:6px;padding:5px}"
            "QHeaderView::section{background:#1a3553;color:#e5efff;padding:5px}"
            "QTableWidget::item:selected{background:#225c96} QProgressBar::chunk{background:#249be9}"
            "QPushButton:focus,QComboBox:focus,QLineEdit:focus{border:2px solid #b4e4ff}");
    };
    QObject::connect(theme, &QComboBox::currentIndexChanged, &window, apply_theme);
    theme->setCurrentIndex(settings.value("appearance", 0).toInt() == 1 ? 1 : 0);
    apply_theme(theme->currentIndex());

    QObject::connect(execute, &QPushButton::clicked, &window, [&] {
        const int selected = selected_operation;
        const auto chosen_disk = [&](QComboBox* combo) -> std::optional<oi::Disk> {
            if (combo->isHidden() || combo->currentIndex() <= 0) return std::nullopt;
            return disks.at(combo->currentData().toInt());
        };
        const auto input_disk = chosen_disk(source_devices);
        const auto output_disk = chosen_disk(target_devices);
        const auto input = native_path(source->text());
        const auto output = native_path(target->text());
        if ((!input_disk && input.empty()) || (uses_target(selected) && !output_disk && output.empty())) {
            status->setText("Select the source disk and destination first.");
            return;
        }
        const auto input_label = input_disk ? source_devices->currentText() : display_path(input);
        const auto output_label = output_disk ? target_devices->currentText() : display_path(output);
        QMessageBox review(output_disk ? QMessageBox::Warning : QMessageBox::Question,
                           "Review operation", operation_names[selected], QMessageBox::Ok | QMessageBox::Cancel, &window);
        review.setTextFormat(Qt::PlainText);
        review.setInformativeText(input_label + (uses_target(selected) ? "\n" + output_label : QString{}) +
            (output_disk ? "\n\nAll data on the destination disk will be overwritten." : ""));
        review.setDefaultButton(QMessageBox::Cancel);
        if (review.exec() != QMessageBox::Ok) return;
        if (output_disk) {
            const auto path = display_path(output_disk->path);
            bool accepted = false;
            const auto typed = QInputDialog::getText(&window, "Confirm disk overwrite",
                "Type the destination device path to confirm:\n" + path, QLineEdit::Normal, {}, &accepted);
            if (!accepted || typed != path) return;
        }
        constexpr oi::Codec codecs[]{oi::Codec::zstd, oi::Codec::raw, oi::Codec::lz4, oi::Codec::lzma2};
        const auto compression = codecs[codec->currentIndex()];
        execute->setEnabled(false);
        progress->show();
        progress->setRange(0, selected == Backup || selected == Restore || selected == Clone ? 100 : 0);
        progress->setValue(0);
        status->setText("Processing and checking data…");
        watcher.setFuture(QtConcurrent::run([=](QPromise<Result>& promise) {
            promise.setProgressRange(0, 100);
            const oi::Progress report = [&](std::uint64_t bytes, std::uint64_t total) {
                promise.setProgressValue(static_cast<int>(100.0 * static_cast<double>(bytes) /
                                                         static_cast<double>(total)));
            };
            const auto run = [&]() -> Result {
                try {
                    switch (selected) {
                    case Backup: oi::backup(input, output, compression, report, input_disk ? &*input_disk : nullptr); break;
                    case Restore: oi::restore(input, output, report, output_disk ? &*output_disk : nullptr); break;
                    case Clone: oi::clone(input, output, report, input_disk ? &*input_disk : nullptr,
                                          output_disk ? &*output_disk : nullptr); break;
                    case Compare: oi::compare(input, output); break;
                    case Verify:
                    case Explore: {
                        const auto info = selected == Verify ? oi::verify(input) : oi::inspect(input);
                        return {QString("%1 | %2 bytes | %3")
                            .arg(selected == Verify ? "Verified" : "Unverified header")
                            .arg(info.bytes).arg(oi::codec_name(info.codec)), true};
                    }
                    default: return {"Unknown operation", false};
                    }
                    return {"Operation completed and data verified.", true};
                } catch (const std::exception& error) {
                    return {QString::fromUtf8(error.what()), false};
                }
            };
            promise.addResult(run());
        }));
    });
    QObject::connect(&watcher, &QFutureWatcher<Result>::progressValueChanged, progress, &QProgressBar::setValue);
    QObject::connect(&watcher, &QFutureWatcher<Result>::finished, &window, [&] {
        const auto result = watcher.result();
        progress->setRange(0, 100);
        progress->setValue(result.ok ? 100 : 0);
        status->setText(result.message);
        execute->setEnabled(true);
    });
#ifdef OPENIMAGE_CAPTURE_TEST
    window.setAttribute(Qt::WA_DontShowOnScreen);
#endif
    window.show();
#ifdef OPENIMAGE_CAPTURE_TEST
    const QDir directory(QString::fromLocal8Bit(argv[1]));
    if (!directory.exists()) return 2;
    application.processEvents();
    inventory.waitForFinished();
    application.processEvents();
    if (source_devices->count() > 1) {
        table->setCurrentCell(source_devices->itemData(1).toInt(), 0);
        select_operation(Backup);
        if (source_devices->currentIndex() != 1 || !source->isHidden()) return 3;
        select_operation(Clone);
        if (source_devices->currentIndex() != 1 || !source->isHidden()) return 4;
        pages->setCurrentWidget(dashboard);
    }
    const char* names[]{"professional.png", "lcars.png"};
    for (int index = 0; index < 2; ++index) {
        theme->setCurrentIndex(index);
        application.processEvents();
        if (!window.grab().save(directory.filePath(names[index]))) return 1;
    }
    return 0;
#else
    const int result = application.exec();
    watcher.waitForFinished();
    inventory.waitForFinished();
    return result;
#endif
}




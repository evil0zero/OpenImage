#include "engine.hpp"

#include <QApplication>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMetaObject>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>
#include <QtConcurrentRun>
#ifdef OPENIMAGE_CAPTURE_TEST
#include <QDir>
#include <QPixmap>
#endif

namespace {
enum Operation { Backup, Restore, Clone, Verify, Explore, Compare };

oi::fs::path native_path(const QString& text) {
#ifdef _WIN32
    return oi::fs::path(text.toStdWString());
#else
    return oi::fs::path(text.toUtf8().toStdString());
#endif
}

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
    window.setWindowTitle("OpenImage | Regular-file imaging");
    window.resize(760, 400);
    auto* layout = new QVBoxLayout(&window);
    auto* title = new QLabel("OPENIMAGE   /   IMAGING CONSOLE");
    title->setObjectName("title");
    layout->addWidget(title);
    auto* summary = new QLabel("Create, restore and verify raw images of regular files.");
    layout->addWidget(summary);

    auto* form = new QFormLayout;
    layout->addLayout(form);
    auto* operation = new QComboBox;
    operation->addItems({"Backup", "Restore", "Clone", "Verify", "Explore header", "Compare"});
    form->addRow("&Operation", operation);
    auto* source = new QLineEdit;
    auto* target = new QLineEdit;
    auto add_path = [&](const QString& label, QLineEdit* edit, bool secondary) {
        auto* row = new QHBoxLayout;
        auto* browse = new QPushButton("Browse…");
        row->addWidget(edit);
        row->addWidget(browse);
        auto* caption = new QLabel(label);
        caption->setBuddy(edit);
        form->addRow(caption, row);
        QObject::connect(browse, &QPushButton::clicked, &window, [&, edit, secondary] {
            const bool new_file = secondary && operation->currentIndex() != Compare;
            const auto path = new_file ? QFileDialog::getSaveFileName(&window, "Choose new output") :
                QFileDialog::getOpenFileName(&window, "Choose input file");
            if (!path.isEmpty()) edit->setText(path);
        });
        return browse;
    };
    add_path("&Input", source, false);
    auto* target_button = add_path("&Second file", target, true);
    auto* codec = new QComboBox;
    codec->addItems({"Zstandard", "Raw", "LZ4", "LZMA2"});
    form->addRow("&Compression", codec);
    auto* theme = new QComboBox;
    theme->addItems({"Professional", "LCARS"});
    form->addRow("&Appearance", theme);

    auto* status = new QLabel("Choose an operation and review the paths before execution.");
    status->setTextFormat(Qt::PlainText);
    status->setWordWrap(true);
    auto* progress = new QProgressBar;
    auto* execute = new QPushButton("Review and run");
    layout->addWidget(progress);
    layout->addWidget(execute);
    layout->addWidget(status);
    layout->addStretch();
    QFutureWatcher<Result> watcher;
    QObject::connect(operation, &QComboBox::currentIndexChanged, &window, [&](int index) {
        target->setEnabled(uses_target(index));
        target_button->setEnabled(uses_target(index));
        codec->setEnabled(index == Backup);
    });
    const auto apply_theme = [&](int index) {
#ifndef OPENIMAGE_CAPTURE_TEST
        settings.setValue("appearance", index);
#endif
        window.setStyleSheet(index == 1 ?
            "QWidget{background:#08070b;color:#fff4df} QLabel#title{background:#f9aa66;"
            "color:#181020;border-radius:18px;padding:16px;font-size:22px;font-weight:bold}"
            "QPushButton,QComboBox{background:#573d72;border:2px solid #bb9bff;border-radius:13px;padding:7px}"
            "QLineEdit,QProgressBar{background:#22294c;border:1px solid #789af2;padding:6px}"
            "QProgressBar::chunk{background:#ffad6b} QWidget:focus{border:2px solid #fff4df}" :
            "QWidget{background:#101d31;color:#e5efff} QLabel#title{background:#172e4c;"
            "padding:16px;font-size:22px;font-weight:bold} QPushButton,QComboBox{background:#234368;"
            "border:1px solid #668eb6;padding:7px} QLineEdit,QProgressBar{background:#152a42;"
            "border:1px solid #668eb6;padding:6px} QProgressBar::chunk{background:#5ebcf4}"
            "QWidget:focus{border:2px solid #b4e4ff}");
    };
    QObject::connect(theme, &QComboBox::currentIndexChanged, &window, apply_theme);
    theme->setCurrentIndex(settings.value("appearance", 0).toInt() == 1 ? 1 : 0);
    apply_theme(theme->currentIndex());

    QObject::connect(execute, &QPushButton::clicked, &window, [&] {
        const int selected = operation->currentIndex();
        if (source->text().isEmpty() || (uses_target(selected) && target->text().isEmpty())) {
            status->setText("Select the required files first.");
            return;
        }
        QMessageBox review(QMessageBox::Question, "Review operation", operation->currentText(),
                           QMessageBox::Ok | QMessageBox::Cancel, &window);
        review.setTextFormat(Qt::PlainText);
        review.setInformativeText(source->text() +
            (uses_target(selected) ? "\n" + target->text() : QString{}));
        review.setDefaultButton(QMessageBox::Cancel);
        if (review.exec() != QMessageBox::Ok) return;
        const auto input = native_path(source->text());
        const auto output = native_path(target->text());
        constexpr oi::Codec codecs[]{oi::Codec::zstd, oi::Codec::raw, oi::Codec::lz4, oi::Codec::lzma2};
        const auto compression = codecs[codec->currentIndex()];
        execute->setEnabled(false);
        progress->setRange(0, 0);
        status->setText("Processing and checking data…");
        watcher.setFuture(QtConcurrent::run([=]() -> Result {
            try {
                switch (selected) {
                case Backup: oi::backup(input, output, compression); break;
                case Restore: oi::restore(input, output); break;
                case Clone: oi::clone(input, output); break;
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
        }));
    });
    QObject::connect(&watcher, &QFutureWatcher<Result>::finished, &window, [&] {
        const auto result = watcher.result();
        progress->setRange(0, 100);
        progress->setValue(result.ok ? 100 : 0);
        status->setText(result.message);
        execute->setEnabled(true);
    });
    window.show();
#ifdef OPENIMAGE_CAPTURE_TEST
    const QDir directory(QString::fromLocal8Bit(argv[1]));
    if (!directory.exists()) return 2;
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
    return result;
#endif
}




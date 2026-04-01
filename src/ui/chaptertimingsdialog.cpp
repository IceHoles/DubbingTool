#include "chaptertimingsdialog.h"

#include <QApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWidget>
#include <algorithm>

ChapterTimingsDialog::ChapterTimingsDialog(const QList<ChapterTimingSeconds>& timings, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Тайминги глав"));

    auto* rootLayout = new QVBoxLayout(this);
    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);

    auto* contentWidget = new QWidget(scrollArea);
    m_rowsLayout = new QVBoxLayout(contentWidget);
    m_rowsLayout->setContentsMargins(8, 8, 8, 8);
    m_rowsLayout->setSpacing(6);

    if (timings.isEmpty())
    {
        auto* label = new QLabel(QStringLiteral("Главы не найдены."), contentWidget);
        m_rowsLayout->addWidget(label);
    }
    else
    {
        for (const ChapterTimingSeconds& row : timings)
        {
            auto* line = new QWidget(contentWidget);
            auto* lineLayout = new QHBoxLayout(line);
            lineLayout->setContentsMargins(0, 0, 0, 0);
            lineLayout->setSpacing(4);

            auto* titleLabel = new QLabel(row.title.isEmpty() ? QStringLiteral("Chapter") : row.title, line);
            auto* startValueLabel = new QLabel(QString::number(row.startSeconds), line);
            auto* endValueLabel = new QLabel(QString::number(row.endSeconds), line);
            auto* copyStartButton = new QPushButton(QStringLiteral("Копировать"), line);
            auto* copyEndButton = new QPushButton(QStringLiteral("Копировать"), line);
            titleLabel->setMinimumWidth(160);
            titleLabel->setMaximumWidth(260);
            startValueLabel->setMinimumWidth(45);
            endValueLabel->setMinimumWidth(45);
            copyStartButton->setMinimumWidth(95);
            copyEndButton->setMinimumWidth(95);

            connect(copyStartButton, &QPushButton::clicked, this,
                    [row]() { QApplication::clipboard()->setText(QString::number(row.startSeconds)); });
            connect(copyEndButton, &QPushButton::clicked, this,
                    [row]() { QApplication::clipboard()->setText(QString::number(row.endSeconds)); });

            lineLayout->addWidget(titleLabel, 0);
            lineLayout->addWidget(new QLabel(QStringLiteral("Начало:"), line));
            lineLayout->addWidget(startValueLabel);
            lineLayout->addWidget(copyStartButton);
            lineLayout->addSpacing(4);
            lineLayout->addWidget(new QLabel(QStringLiteral("Конец:"), line));
            lineLayout->addWidget(endValueLabel);
            lineLayout->addWidget(copyEndButton);
            lineLayout->addStretch(1);
            m_rowsLayout->addWidget(line);
        }
    }

    m_rowsLayout->addStretch();
    contentWidget->setLayout(m_rowsLayout);
    scrollArea->setWidget(contentWidget);
    rootLayout->addWidget(scrollArea);

    const int rowCount = timings.isEmpty() ? 1 : static_cast<int>(timings.size());
    const int visibleRows = std::clamp(rowCount, 1, 10);
    const int targetHeight = 110 + (visibleRows * 34);
    const int targetWidth = std::clamp(contentWidget->sizeHint().width() + 60, 620, 980);
    resize(targetWidth, targetHeight);
}

#pragma once

#include <QWidget>

namespace calango::gui {

/// Zone-1 branding card: the Calango logo, name and version. Purely
/// informative — it keeps the top-left corner of the 12-zone grid from
/// competing with the data panels.
class BrandingPanel : public QWidget {
    Q_OBJECT

public:
    explicit BrandingPanel(QWidget* parent = nullptr);
};

} // namespace calango::gui

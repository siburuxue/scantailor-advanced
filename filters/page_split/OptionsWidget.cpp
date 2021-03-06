/*
    Scan Tailor - Interactive post-processing tool for scanned pages.
    Copyright (C)  Joseph Artsimovich <joseph.artsimovich@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "OptionsWidget.h"
#include <cassert>
#include <utility>
#include "Filter.h"
#include "PageLayoutAdapter.h"
#include "ProjectPages.h"
#include "ScopedIncDec.h"
#include "Settings.h"
#include "SplitModeDialog.h"

namespace page_split {
OptionsWidget::OptionsWidget(intrusive_ptr<Settings> settings,
                             intrusive_ptr<ProjectPages> page_sequence,
                             const PageSelectionAccessor& page_selection_accessor)
    : m_settings(std::move(settings)),
      m_pages(std::move(page_sequence)),
      m_pageSelectionAccessor(page_selection_accessor),
      m_ignoreAutoManualToggle(0),
      m_ignoreLayoutTypeToggle(0) {
  setupUi(this);
  // Workaround for QTBUG-182
  auto* grp = new QButtonGroup(this);
  grp->addButton(autoBtn);
  grp->addButton(manualBtn);

  setupUiConnections();
}

OptionsWidget::~OptionsWidget() = default;

void OptionsWidget::preUpdateUI(const PageId& page_id) {
  removeUiConnections();

  ScopedIncDec<int> guard1(m_ignoreAutoManualToggle);
  ScopedIncDec<int> guard2(m_ignoreLayoutTypeToggle);

  m_pageId = page_id;
  const Settings::Record record(m_settings->getPageRecord(page_id.imageId()));
  const LayoutType layout_type(record.combinedLayoutType());

  switch (layout_type) {
    case AUTO_LAYOUT_TYPE:
      // Uncheck all buttons.  Can only be done
      // by playing with exclusiveness.
      twoPagesBtn->setChecked(true);
      twoPagesBtn->setAutoExclusive(false);
      twoPagesBtn->setChecked(false);
      twoPagesBtn->setAutoExclusive(true);
      break;
    case SINGLE_PAGE_UNCUT:
      singlePageUncutBtn->setChecked(true);
      break;
    case PAGE_PLUS_OFFCUT:
      pagePlusOffcutBtn->setChecked(true);
      break;
    case TWO_PAGES:
      twoPagesBtn->setChecked(true);
      break;
  }

  splitLineGroup->setVisible(layout_type != SINGLE_PAGE_UNCUT);

  if (layout_type == AUTO_LAYOUT_TYPE) {
    changeBtn->setEnabled(false);
    scopeLabel->setText("?");
  } else {
    changeBtn->setEnabled(true);
    scopeLabel->setText(tr("Set manually"));
  }

  // Uncheck both the Auto and Manual buttons.
  autoBtn->setChecked(true);
  autoBtn->setAutoExclusive(false);
  autoBtn->setChecked(false);
  autoBtn->setAutoExclusive(true);
  // And disable both of them.
  autoBtn->setEnabled(false);
  manualBtn->setEnabled(false);

  setupUiConnections();
}  // OptionsWidget::preUpdateUI

void OptionsWidget::postUpdateUI(const UiData& ui_data) {
  removeUiConnections();

  ScopedIncDec<int> guard1(m_ignoreAutoManualToggle);
  ScopedIncDec<int> guard2(m_ignoreLayoutTypeToggle);

  m_uiData = ui_data;

  changeBtn->setEnabled(true);
  autoBtn->setEnabled(true);
  manualBtn->setEnabled(true);

  if (ui_data.splitLineMode() == MODE_AUTO) {
    autoBtn->setChecked(true);
  } else {
    manualBtn->setChecked(true);
  }

  const PageLayout::Type layout_type = ui_data.pageLayout().type();

  switch (layout_type) {
    case PageLayout::SINGLE_PAGE_UNCUT:
      singlePageUncutBtn->setChecked(true);
      break;
    case PageLayout::SINGLE_PAGE_CUT:
      pagePlusOffcutBtn->setChecked(true);
      break;
    case PageLayout::TWO_PAGES:
      twoPagesBtn->setChecked(true);
      break;
  }

  splitLineGroup->setVisible(layout_type != PageLayout::SINGLE_PAGE_UNCUT);

  if (ui_data.layoutTypeAutoDetected()) {
    scopeLabel->setText(tr("Auto detected"));
  }

  setupUiConnections();
}  // OptionsWidget::postUpdateUI

void OptionsWidget::pageLayoutSetExternally(const PageLayout& page_layout) {
  ScopedIncDec<int> guard(m_ignoreAutoManualToggle);

  m_uiData.setPageLayout(page_layout);
  m_uiData.setSplitLineMode(MODE_MANUAL);
  commitCurrentParams();

  manualBtn->setChecked(true);

  emit invalidateThumbnail(m_pageId);
}

void OptionsWidget::layoutTypeButtonToggled(const bool checked) {
  if (!checked || m_ignoreLayoutTypeToggle) {
    return;
  }

  LayoutType lt;
  ProjectPages::LayoutType plt = ProjectPages::ONE_PAGE_LAYOUT;

  QObject* button = sender();
  if (button == singlePageUncutBtn) {
    lt = SINGLE_PAGE_UNCUT;
  } else if (button == pagePlusOffcutBtn) {
    lt = PAGE_PLUS_OFFCUT;
  } else {
    assert(button == twoPagesBtn);
    lt = TWO_PAGES;
    plt = ProjectPages::TWO_PAGE_LAYOUT;
  }

  Settings::UpdateAction update;
  update.setLayoutType(lt);

  splitLineGroup->setVisible(lt != SINGLE_PAGE_UNCUT);
  scopeLabel->setText(tr("Set manually"));

  m_pages->setLayoutTypeFor(m_pageId.imageId(), plt);

  if ((lt == PAGE_PLUS_OFFCUT) || ((lt != SINGLE_PAGE_UNCUT) && (m_uiData.splitLineMode() == MODE_AUTO))) {
    m_settings->updatePage(m_pageId.imageId(), update);
    emit reloadRequested();
  } else {
    PageLayout::Type plt;
    if (lt == SINGLE_PAGE_UNCUT) {
      plt = PageLayout::SINGLE_PAGE_UNCUT;
    } else {
      assert(lt == TWO_PAGES);
      plt = PageLayout::TWO_PAGES;
    }

    PageLayout new_layout(m_uiData.pageLayout());
    new_layout.setType(plt);
    const Params new_params(new_layout, m_uiData.dependencies(), m_uiData.splitLineMode());

    update.setParams(new_params);
    m_settings->updatePage(m_pageId.imageId(), update);

    m_uiData.setPageLayout(new_layout);

    emit pageLayoutSetLocally(new_layout);
    emit invalidateThumbnail(m_pageId);
  }
}  // OptionsWidget::layoutTypeButtonToggled

void OptionsWidget::showChangeDialog() {
  const Settings::Record record(m_settings->getPageRecord(m_pageId.imageId()));
  const Params* params = record.params();
  if (!params) {
    return;
  }

  auto* dialog = new SplitModeDialog(this, m_pageId, m_pageSelectionAccessor, record.combinedLayoutType(),
                                     params->pageLayout().type(), params->splitLineMode() == MODE_AUTO);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  connect(dialog, SIGNAL(accepted(const std::set<PageId>&, LayoutType, bool)), this,
          SLOT(layoutTypeSet(const std::set<PageId>&, LayoutType, bool)));
  dialog->show();
}

void OptionsWidget::layoutTypeSet(const std::set<PageId>& pages, const LayoutType layout_type, bool apply_cut) {
  if (pages.empty()) {
    return;
  }

  const Params params = *(m_settings->getPageRecord(m_pageId.imageId()).params());

  if (layout_type != AUTO_LAYOUT_TYPE) {
    for (const PageId& page_id : pages) {
      Settings::UpdateAction update_action;
      update_action.setLayoutType(layout_type);
      if (apply_cut && (layout_type != SINGLE_PAGE_UNCUT)) {
        Params new_page_params(params);
        const Params* old_page_params = m_settings->getPageRecord(page_id.imageId()).params();
        if (old_page_params) {
          PageLayout new_page_layout = PageLayoutAdapter::adaptPageLayout(
              params.pageLayout(), old_page_params->pageLayout().uncutOutline().boundingRect());

          update_action.setLayoutType(new_page_layout.toLayoutType());
          new_page_params.setPageLayout(new_page_layout);

          Dependencies deps = old_page_params->dependencies();
          deps.setLayoutType(new_page_layout.toLayoutType());
          new_page_params.setDependencies(deps);
        }
        update_action.setParams(new_page_params);
      }
      m_settings->updatePage(page_id.imageId(), update_action);
    }
  } else {
    for (const PageId& page_id : pages) {
      Settings::UpdateAction update_params;
      update_params.setLayoutType(layout_type);
      m_settings->updatePage(page_id.imageId(), update_params);
    }
  }

  if (pages.size() > 1) {
    emit invalidateAllThumbnails();
  } else {
    for (const PageId& page_id : pages) {
      emit invalidateThumbnail(page_id);
    }
  }

  if (layout_type == AUTO_LAYOUT_TYPE) {
    scopeLabel->setText(tr("Auto detected"));
    emit reloadRequested();
  } else {
    scopeLabel->setText(tr("Set manually"));
  }
}  // OptionsWidget::layoutTypeSet

void OptionsWidget::splitLineModeChanged(const bool auto_mode) {
  if (m_ignoreAutoManualToggle) {
    return;
  }

  if (auto_mode) {
    Settings::UpdateAction update;
    update.clearParams();
    m_settings->updatePage(m_pageId.imageId(), update);
    m_uiData.setSplitLineMode(MODE_AUTO);
    emit reloadRequested();
  } else {
    m_uiData.setSplitLineMode(MODE_MANUAL);
    commitCurrentParams();
  }
}

void OptionsWidget::commitCurrentParams() {
  const Params params(m_uiData.pageLayout(), m_uiData.dependencies(), m_uiData.splitLineMode());
  Settings::UpdateAction update;
  update.setParams(params);
  m_settings->updatePage(m_pageId.imageId(), update);
}

#define CONNECT(...) m_connectionList.push_back(connect(__VA_ARGS__));

void OptionsWidget::setupUiConnections() {
  CONNECT(singlePageUncutBtn, SIGNAL(toggled(bool)), this, SLOT(layoutTypeButtonToggled(bool)));
  CONNECT(pagePlusOffcutBtn, SIGNAL(toggled(bool)), this, SLOT(layoutTypeButtonToggled(bool)));
  CONNECT(twoPagesBtn, SIGNAL(toggled(bool)), this, SLOT(layoutTypeButtonToggled(bool)));
  CONNECT(changeBtn, SIGNAL(clicked()), this, SLOT(showChangeDialog()));
  CONNECT(autoBtn, SIGNAL(toggled(bool)), this, SLOT(splitLineModeChanged(bool)));
}

#undef CONNECT

void OptionsWidget::removeUiConnections() {
  for (const auto& connection : m_connectionList) {
    disconnect(connection);
  }
  m_connectionList.clear();
}

/*============================= Widget::UiData ==========================*/

OptionsWidget::UiData::UiData() : m_splitLineMode(MODE_AUTO), m_layoutTypeAutoDetected(false) {}

OptionsWidget::UiData::~UiData() = default;

void OptionsWidget::UiData::setPageLayout(const PageLayout& layout) {
  m_pageLayout = layout;
}

const PageLayout& OptionsWidget::UiData::pageLayout() const {
  return m_pageLayout;
}

void OptionsWidget::UiData::setDependencies(const Dependencies& deps) {
  m_deps = deps;
}

const Dependencies& OptionsWidget::UiData::dependencies() const {
  return m_deps;
}

void OptionsWidget::UiData::setSplitLineMode(const AutoManualMode mode) {
  m_splitLineMode = mode;
}

AutoManualMode OptionsWidget::UiData::splitLineMode() const {
  return m_splitLineMode;
}

bool OptionsWidget::UiData::layoutTypeAutoDetected() const {
  return m_layoutTypeAutoDetected;
}

void OptionsWidget::UiData::setLayoutTypeAutoDetected(const bool val) {
  m_layoutTypeAutoDetected = val;
}
}  // namespace page_split
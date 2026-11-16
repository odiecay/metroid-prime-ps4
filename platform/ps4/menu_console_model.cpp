// SPDX-License-Identifier: MIT
#include "port_ps4_menu_model.h"
#include "menu_console_model.hpp"

CModel* PortPS4Menu::CreateModel(IObjectStore& store) {
  return prime::menu::CreatePs4Model(store);
}

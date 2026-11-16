// SPDX-License-Identifier: MIT
#pragma once
#include "menu_console_model_data.hpp"
#include "Kyoto/Graphics/CModel.hpp"
#include "Kyoto/IObjectStore.hpp"
#include "rstl/auto_ptr.hpp"
#include <cstring>

namespace prime::menu {
// CModel takes the copy's ownership and uses its ordinary frame-delayed data
// destruction. The compiled source mesh has no textures or external resources.
inline CModel* CreatePs4Model(IObjectStore& store) {
  rstl::auto_ptr<uchar[]> copy(rs_new uchar[Ps4ModelSize]);
  std::memcpy(copy.get(),Ps4ModelBytes,Ps4ModelSize);
  return rs_new CModel(copy,static_cast<int>(Ps4ModelSize),store);
}
}

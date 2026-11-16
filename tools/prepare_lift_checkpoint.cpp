// Convert a private native resume into a separate, resettable lift test input.
// Uses the game's actual codec. Never modifies the source or a memory card.
#include "port_savestate.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

int main(int argc, char** argv) {
  if (argc != 3 && argc != 4) return 2;
  const bool newConsole = argc == 4 && !std::strcmp(argv[3], "--new-console-slot3");
  const bool entrance = argc == 4 && !std::strcmp(argv[3], "--mech-entrance-slot2");
  const bool approach = argc == 4 && !std::strcmp(argv[3], "--before-lift-room-slot2");
  if (argc == 4 && !newConsole && !entrance && !approach) return 2;
  std::ifstream input(argv[1], std::ios::binary);
  if (!input) return 3;
  const std::string original((std::istreambuf_iterator<char>(input)), {});
  PortSaveState::ResumeMeta meta;
  PortSaveState::Header header;
  std::string state;
  std::vector<uint8_t> blob;
  if (!PortSaveState::DecodeResume(original, meta, state) ||
      !PortSaveState::Decode(state, header, blob) || blob.size() != 4096 ||
      header.worldId != 0x158efe17u || header.areaId != 5 || header.disc != "USA 1.00" ||
      meta.cardSerial == 0 || !meta.identity.empty()) {
    std::fputs("Expected this console's retail USA post-damage lift checkpoint\n", stderr);
    return 4;
  }
  const auto originalBlob = blob;
  if (newConsole) meta.fileIdx = 2; // use an empty diagnostic slot; native checks refuse occupied slots
  // Let the original room initialize its lift and authored spawn. The native
  // test path deliberately ignores the captured fall position in this header.
  if (approach) {
    // Actual shared dock plane from the supplied USA disc: x=59.60524,
    // y center=48.624916, floor=-48.343742. Stand 2 units into area 7.
    meta.fileIdx=1;header.areaId=7;header.room="Lift room approach";
    header.position[0]=61.60524f;header.position[1]=48.624916f;header.position[2]=-48.243742f;
    header.forward[0]=-1.f;header.forward[1]=0.f;header.forward[2]=0.f;
    header.morphed=false;header.roomState={};
  } else if (entrance) {
    const PortSaveState::RoomObject* lift = nullptr;
    for (const auto& object : header.roomState.objects)
      if (object.editorId == 0x00050009u) lift = &object;
    const auto* transform = lift != nullptr ? PortSaveState::FindBlock(*lift, 1) : nullptr;
    if (transform == nullptr || transform->data.size() != 48) return 8;
    PortSaveState::BlockReader reader(transform->data);
    float xf[12];for(float& value : xf) value=reader.F32();
    // The real captured lift is at its upper waypoint. Place above its center
    // and let original gravity settle Samus, facing the Mech Shaft doorway.
    if (!reader.ok || xf[11] < 4.f || xf[11] > 6.f) return 8;
    meta.fileIdx=1;
    header.position[0]=xf[3];header.position[1]=xf[7];header.position[2]=xf[11]+1.5f;
    header.forward[0]=0.f;header.forward[1]=-1.f;header.forward[2]=0.f;
    header.morphed=false;
    header.room="Deck Alpha Mech Shaft entrance";
  } else {
    header.roomState = {};
  }
  const std::string prepared = PortSaveState::EncodeResume(meta, PortSaveState::Encode(header, blob));
  PortSaveState::ResumeMeta checkedMeta;
  PortSaveState::Header checkedHeader;
  std::vector<uint8_t> checkedBlob;
  if (!PortSaveState::DecodeResume(prepared, checkedMeta, state) ||
      !PortSaveState::Decode(state, checkedHeader, checkedBlob) ||
      checkedBlob != originalBlob || checkedMeta.cardSerial != meta.cardSerial ||
      checkedHeader.worldId != header.worldId || checkedHeader.areaId != header.areaId ||
      checkedMeta.fileIdx != meta.fileIdx ||
      checkedHeader.roomState.objects.size() != header.roomState.objects.size() ||
      checkedHeader.roomState.areas != header.roomState.areas) return 5;
  std::ifstream existing(argv[2], std::ios::binary);
  if (existing) return 6;
  std::ofstream output(argv[2], std::ios::binary);
  output.write(prepared.data(), prepared.size());
  output.close();
  if (!output) return 7;
  std::printf("Prepared separate lift checkpoint: area=%d, %zu game-state bytes preserved; "
              "authored spawn, fresh room objects; original unchanged\n", header.areaId, blob.size());
  return 0;
}

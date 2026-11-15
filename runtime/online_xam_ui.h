#pragma once
// online_xam_ui.h (v1137, trainer-mods; lead 09:2x "answer each new system call in bulk"): the system UIs (XamShow*UI) GTA V imports have no
// device equivalent. On the online-local path each answers as a UI that closed at once; the decision is pure (host test), the guest writes are
// in online_local_bridge.inc. Argument layouts read from the game's own call sites (validated-ppc):
//   KeyboardUI (sub_832312A8): user, flags, default, title, description, result, cch, XOVERLAPPED (r10)   -> ERROR_CANCELLED (the user backed out;
//       the result text is left as it is)
//   MessageBoxUI (sub_829995C0): user, title, text, cButtons, buttons, focus, flags, MESSAGEBOX_RESULT* (r10), XOVERLAPPED (stack r1+0x54)
//       -> one button: ERROR_SUCCESS, dwButtonPressed 0 (OK); several: ERROR_CANCELLED (no choice is made for the player)
//   MarketplaceDownloadItemsUI (sub_834B4598): user, entry, offer ids, count, HRESULT* (r7), XOVERLAPPED (r8) -> ERROR_CANCELLED, *hr =
//       HRESULT_FROM_WIN32(ERROR_CANCELLED)
//   MarketplaceUI (sub_834A1CC8), FriendRequestUI (sub_82F9A718/788), PlayerReviewUI (sub_83275EB0), MessageComposeUI (sub_8326DD08), and
//   GamerCardUIForXUID (v1136) -> ERROR_SUCCESS, no overlapped.
// Overlapped completion as the bounded registry does it (guest_saves_dispatch.inc): result / length / extended error written, the event set, the
// call returns ERROR_IO_PENDING; an APC completion routine is not modelled (the guarded stop). Not answered: XamShowMessageBoxUIEx (its only caller
// is library code sub_83639628 with another layout) and XamShowDirtyDiscErrorUI (the fatal read-error screen): both keep the guarded stop.
#include <cstdint>
namespace recomp::xam_ui {
constexpr std::uint32_t ErrorSuccess=0u,ErrorCancelled=1223u,IoPending=997u;
constexpr std::uint32_t hresultOf(std::uint32_t win32) noexcept {return win32==0u?0u:(0x80070000u|(win32&0xFFFFu));}
enum class Kind : std::uint8_t {Plain,Keyboard,MessageBox,MarketDownload};
struct Answer {std::uint32_t result;std::uint32_t button;bool writeButton;bool writeHresult;};
constexpr Answer decide(Kind k,std::uint32_t buttons) noexcept {
    switch(k) {
    case Kind::Plain: return {ErrorSuccess,0u,false,false};
    case Kind::Keyboard: return {ErrorCancelled,0u,false,false};
    case Kind::MessageBox: return buttons==1u?Answer{ErrorSuccess,0u,true,false}:Answer{ErrorCancelled,0u,false,false};
    case Kind::MarketDownload: return {ErrorCancelled,0u,false,true};
    }
    return {ErrorCancelled,0u,false,false};
}
}

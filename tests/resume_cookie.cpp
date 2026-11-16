#include "../platform/ps4/resume_cookie.hpp"
#include <cassert>
int main() {
  std::string state="a safe serialized game state";
  const auto token=prime::resume_cookie(state);
  assert(prime::resume_cookie_matches(state,token));
  state[0]='x';assert(!prime::resume_cookie_matches(state,token));
  state[0]='a';state.pop_back();assert(!prime::resume_cookie_matches(state,token));
  assert(!prime::resume_cookie_matches("",prime::resume_cookie("")));
  assert(!prime::resume_cookie_matches(state,""));
}

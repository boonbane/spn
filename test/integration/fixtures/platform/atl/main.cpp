#include <atlbase.h>

int main() {
  CComPtr<IUnknown> unknown;
  CComBSTR name(L"A");
  return name.Length() == 1 && !unknown ? 0 : 1;
}

#include "grepmorph.h"

const char *gm_morph_name(gm_morph morph) {
    switch(morph) {
        case GM_MORPH_RAW:            return "raw";
        case GM_MORPH_UTF8:           return "utf8";
        case GM_MORPH_UTF16_LE:       return "utf16-le";
        case GM_MORPH_UTF16_BE:       return "utf16-be";
        case GM_MORPH_HEX_TEXT:       return "hex-text";
        case GM_MORPH_BASE64_DECODED: return "base64-decoded";
        case GM_MORPH_UINT_LE:        return "uint-le";
        case GM_MORPH_UINT_BE:        return "uint-be";
        default:                      return "unknown";
    }
}

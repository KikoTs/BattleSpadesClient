#include "battlespades/frontend/ugc_prefab_catalog.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <span>
#include <string_view>

namespace battlespades::frontend {
namespace {

/**
 * Case-folded FNV-1a hashes generated from the 448 exact keys in retail
 * shared/constants_prefabs.py:PREFABS_NAMES_WITH_TAGS.  The source set has no
 * collisions under this hash.  Hashes keep the generated catalog compact while
 * retaining source-defined membership instead of guessing from prefab names.
 */
[[nodiscard]] std::uint32_t prefab_hash(std::string_view value) noexcept {
    std::uint32_t result{0x811C9DC5U};
    for (const auto raw : value) {
        const auto byte = static_cast<unsigned char>(raw);
        result ^= static_cast<std::uint8_t>(std::tolower(byte));
        result *= 0x01000193U;
    }
    return result;
}

constexpr std::array<std::uint32_t, 168U> landscape{{
    0x10461610U, 0x114617A3U, 0x12461936U, 0x13461AC9U, 0x13485960U, 0x14461C5CU,
    0x14485AF3U, 0x15461DEFU, 0x16461F82U, 0x17462115U, 0x1B4865F8U, 0x1C48678BU,
    0x1D48691EU, 0x1E462C1AU, 0x1E486AB1U, 0x1F462DADU, 0x1F486C44U, 0x1F4CE972U,
    0x20486DD7U, 0x204CEB05U, 0x21486F6AU, 0x214CEC98U, 0x224870FDU, 0x224AAF94U,
    0x224CEE2BU, 0x234AB127U, 0x234CEFBEU, 0x244AB2BAU, 0x244CF151U, 0x254AB44DU,
    0x254CF2E4U, 0x264CF477U, 0x274CF60AU, 0x284CF79DU, 0x380B9860U, 0x3A0B9B86U,
    0x3B0B9D19U, 0x3C0B9EACU, 0x3D0BA03FU, 0x3E0BA1D2U, 0x3F0BA365U, 0x400BA4F8U,
    0x410BA68BU, 0x5687508CU, 0x5787521FU, 0x5A8756D8U, 0x5B87586BU, 0x5C8759FEU,
    0x5D875B91U, 0x5E875D24U, 0x5F875EB7U, 0x6087604AU, 0x618761DDU, 0x62A2EEB0U,
    0x63A2F043U, 0x64A2F1D6U, 0x65A2F369U, 0x663D3C90U, 0x66A2F4FCU, 0x673D3E23U,
    0x683D3FB6U, 0x68A2F822U, 0x693D4149U, 0x69A2F9B5U, 0x6A3D42DCU, 0x6B3D446FU,
    0x6C3D4602U, 0x6C41C330U, 0x6D3D4795U, 0x6D41C4C3U, 0x6EA30194U, 0x6FA30327U,
    0x703D4C4EU, 0x713D4DE1U, 0x7441CFC8U, 0x7541D15BU, 0x7641D2EEU, 0x7741D481U,
    0x7841D614U, 0x7941D7A7U, 0x7A41D93AU, 0x7B41DACDU, 0x8A40C5F0U, 0x8B40C783U,
    0x8C40C916U, 0x8D40CAA9U, 0x8E40CC3CU, 0x8F40CDCFU, 0x9040CF62U, 0x9140D0F5U,
    0x9240D288U, 0x9340D41BU, 0xA21099E0U, 0xA3109B73U, 0xA4109D06U, 0xA5109E99U,
    0xA512DD30U, 0xA610A02CU, 0xA612DEC3U, 0xA712E056U, 0xA812E1E9U, 0xA8152080U,
    0xA912E37CU, 0xA9152213U, 0xAA12E50FU, 0xAA1523A6U, 0xAB12E6A2U, 0xAB152539U,
    0xAC12E835U, 0xAC1526CCU, 0xAD15285FU, 0xAD6CAA90U, 0xAE1529F2U, 0xAE6CAC23U,
    0xAF12ECEEU, 0xAF152B85U, 0xAF6CADB6U, 0xB012EE81U, 0xB16CB0DCU, 0xB26CB26FU,
    0xB36CB402U, 0xB46CB595U, 0xB615368AU, 0xB715381DU, 0xB76CBA4EU, 0xB86CBBE1U,
    0xCA8C8456U, 0xCB8EC480U, 0xCC8EC613U, 0xCD8EC7A6U, 0xCE8EC939U, 0xCF8ECACCU,
    0xD08ECC5FU, 0xD18ECDF2U, 0xD28ECF85U, 0xD78ED764U, 0xD88ED8F7U, 0xE1E9E970U,
    0xE2E9EB03U, 0xE33A2FC0U, 0xE43A3153U, 0xE4E9EE29U, 0xE53A32E6U, 0xE5493D70U,
    0xE5E9EFBCU, 0xE63A3479U, 0xE6493F03U, 0xE6E9F14FU, 0xE73A360CU, 0xE7494096U,
    0xE7E9F2E2U, 0xE83A379FU, 0xE8494229U, 0xE8E9F475U, 0xE93A3932U, 0xE94943BCU,
    0xEA3A3AC5U, 0xEA49454FU, 0xEB3A3C58U, 0xEB4946E2U, 0xEBE9F92EU, 0xEC3A3DEBU,
    0xEC494875U, 0xEC4B870CU, 0xECE9FAC1U, 0xED4B889FU, 0xEF494D2EU, 0xF0494EC1U,
}};

constexpr std::array<std::uint32_t, 108U> buildings{{
    0x0778341FU, 0x0830F7EFU, 0x0840876DU, 0x09B90E13U, 0x09F81E77U, 0x0B37EFA0U,
    0x0C7E846EU, 0x0CA7A3E5U, 0x0D4C9110U, 0x0E9D2551U, 0x0F83CC9DU, 0x17B1FB24U,
    0x1803D149U, 0x235E026FU, 0x245E0402U, 0x2BA90E3CU, 0x33DE8522U, 0x33E0D6A5U,
    0x34E273FEU, 0x34FAB044U, 0x35136D74U, 0x3664858CU, 0x37FAB4FDU, 0x39648A45U,
    0x401F5D2FU, 0x480B0A23U, 0x4AE49A67U, 0x4D73DBF8U, 0x507ADC4DU, 0x50FFA62BU,
    0x56A2E6CBU, 0x57A2E85EU, 0x57A79AADU, 0x59A44D20U, 0x5B93A630U, 0x5C35681FU,
    0x5C5B9696U, 0x5D202938U, 0x5D3569B2U, 0x5FEDC56AU, 0x61A5CE3EU, 0x64A4E1EDU,
    0x6764CDD5U, 0x68B0A84CU, 0x68F70EA8U, 0x69F7103BU, 0x6A3BA333U, 0x6F83CC76U,
    0x73BAF109U, 0x79C84791U, 0x7AC84924U, 0x7BC84AB7U, 0x7C09D586U, 0x7CC84C4AU,
    0x7D492BEFU, 0x7FE5C13EU, 0x800AFA7DU, 0x826346B0U, 0x83C3D2AFU, 0x83FA4353U,
    0x855E4C8DU, 0x85773183U, 0x86773316U, 0x8C91A4B7U, 0x8DF96F2BU, 0x8E9D5967U,
    0x90F694D0U, 0x91F69663U, 0x9423FB53U, 0x9523FCE6U, 0x95F69CAFU, 0x961F3773U,
    0x96F69E42U, 0x97F69FD5U, 0x9EE4CEF0U, 0xA465D4D7U, 0xA554E94CU, 0xABAFFC55U,
    0xABFCC666U, 0xAFD5867CU, 0xB0A5AB05U, 0xB1D589A2U, 0xB2D58B35U, 0xB48FA3F7U,
    0xB80AF3B5U, 0xBA2D5816U, 0xCC025CE4U, 0xCC3400ABU, 0xCE02600AU, 0xCF02619DU,
    0xD0BAD50EU, 0xD6ACECC1U, 0xD90A2987U, 0xD90FDC38U, 0xDB32C59BU, 0xDC32C72EU,
    0xDE22AFCCU, 0xE26D32BAU, 0xE3DA7F8AU, 0xE9171107U, 0xE951A523U, 0xECBF49A6U,
    0xEEA58A89U, 0xEF13BADAU, 0xF232D738U, 0xF2EF7559U, 0xFD61C097U, 0xFE61C22AU,
}};

constexpr std::array<std::uint32_t, 47U> nature{{
    0x028D7100U, 0x03743653U, 0x083C590FU, 0x0BEAD8F4U, 0x0DEBD45CU, 0x0F308DB3U,
    0x13AAE5A9U, 0x1CACFFBDU, 0x2280F98AU, 0x2761A770U, 0x2A61AC29U, 0x2B931D7CU,
    0x352A6369U, 0x3C960346U, 0x3E14A209U, 0x4754A896U, 0x509AE105U, 0x5554CB25U,
    0x5FBB0AA0U, 0x60BB0C33U, 0x62BB0F59U, 0x62E186A0U, 0x64D92D00U, 0x6D099A2AU,
    0x718F9FDAU, 0x8078925CU, 0x80A660A1U, 0x816CC6E7U, 0x84361276U, 0x883688E2U,
    0x88A77787U, 0x8C6816FEU, 0x91D5C14EU, 0x9AA585FAU, 0xA0EBF96AU, 0xAC1FD55EU,
    0xB27FB144U, 0xC372DB2DU, 0xC7C09647U, 0xCFF97D6BU, 0xD47CBDB8U, 0xDF60C544U,
    0xE1168E0DU, 0xEBAEC4AFU, 0xF1054B7AU, 0xF1FFB176U, 0xFB9EA6E4U,
}};

constexpr std::array<std::uint32_t, 70U> props{{
    0x05FF940FU, 0x0D288DCFU, 0x0DC9420AU, 0x0ED39CEBU, 0x0F6B0331U, 0x118D26B4U,
    0x11B99FA1U, 0x122F7A71U, 0x148D2B6DU, 0x1A17E1B1U, 0x1CE6704AU, 0x1E88B147U,
    0x1EB34232U, 0x1F112D7BU, 0x1F88B2DAU, 0x2524729FU, 0x3166AB73U, 0x3564EE1CU,
    0x38B130E5U, 0x3DE134ADU, 0x3FC6B859U, 0x40613DCAU, 0x46518E88U, 0x469140A4U,
    0x46E9C1A9U, 0x5D8AC931U, 0x5E0E9747U, 0x61C40DE3U, 0x67C49B1DU, 0x68E09489U,
    0x6BE09942U, 0x6D2FC4E9U, 0x724EEFCAU, 0x756B734BU, 0x7588FDFCU, 0x7924DE2EU,
    0x7A5EFBD8U, 0x7BB09A46U, 0x7BBAAB3BU, 0x7CDACA53U, 0x7D84B69BU, 0x7FE12133U,
    0x80E122C6U, 0x8193085AU, 0x8498A009U, 0x8999DB07U, 0x8B139372U, 0x94B95C0BU,
    0x95596ACEU, 0x9EC65748U, 0xA61D9F07U, 0xA97CCB39U, 0xB8622F13U, 0xB9F9020DU,
    0xBB0535A4U, 0xBE61FA2CU, 0xC9BB3779U, 0xD314465DU, 0xD6115F9BU, 0xD92C4869U,
    0xE8B63CCFU, 0xEBB1764CU, 0xF1AF7B47U, 0xF1C4EC27U, 0xF3C303EEU, 0xF58D77D4U,
    0xF6A34B79U, 0xF7256976U, 0xF76D3C06U, 0xF9262922U,
}};

constexpr std::array<std::uint32_t, 30U> roads{{
    0x0080E065U, 0x0CE1000AU, 0x37C2A09CU, 0x43802857U, 0x5DF3483EU, 0x64F96F67U,
    0x6A959518U, 0x75C28CC3U, 0x7AB8CB58U, 0x7BB8CCEBU, 0x80B8D4CAU, 0x8138FCF8U,
    0x85867E7AU, 0x859538EFU, 0x8BA04F82U, 0x8F44155BU, 0x94730649U, 0x9B8CA650U,
    0x9BC5A804U, 0xA1A7A8FBU, 0xA2A7AA8EU, 0xA3A7AC21U, 0xAB467AC5U, 0xAB5B2E45U,
    0xAC05EB85U, 0xCD24A9B4U, 0xD2802C8CU, 0xE6D54D2BU, 0xEA7265C0U, 0xF8ED3D4EU,
}};

constexpr std::array<std::uint32_t, 25U> signs{{
    0x01BBA35FU, 0x142123BBU, 0x17212874U, 0x61AF3097U, 0x67EE33BEU, 0x6CEE3B9DU,
    0x78D161D8U, 0x7DD169B7U, 0xA99B996EU, 0xB01A8D50U, 0xD257AE8EU, 0xD686A1A0U,
    0xD73339A6U, 0xD757B66DU, 0xDB86A97FU, 0xDC334185U, 0xDC3951B3U, 0xDC448AA6U,
    0xDF39566CU, 0xE1449285U, 0xF5A5315EU, 0xF935ADC3U, 0xFAA5393DU, 0xFC35B27CU,
    0xFCBB9B80U,
}};

struct PrefabMetadata final {
    std::uint32_t hash{};
    std::string_view display_name;
    std::string_view size_key;
};

// Generated directly from the 448 retail prefab identifiers in
// constants_prefabs.py, their english.py display strings, and the voxel count
// stored at byte 28 of the corresponding shipped KV6. Keep the generated
// records in a separate include so the category evidence above stays readable.
constexpr std::array<PrefabMetadata, 448U> metadata{{
#include "ugc_prefab_metadata.generated.inc"
}};

[[nodiscard]] bool has(std::span<const std::uint32_t> values,
                       std::uint32_t value) noexcept {
    return std::ranges::binary_search(values, value);
}

} // namespace

std::optional<UgcPrefabCategory>
ugc_prefab_category(std::string_view prefab_name) noexcept {
    const auto hash = prefab_hash(prefab_name);
    if (has(landscape, hash)) return UgcPrefabCategory::landscape;
    if (has(buildings, hash)) return UgcPrefabCategory::buildings_and_walls;
    if (has(nature, hash)) return UgcPrefabCategory::nature;
    if (has(props, hash)) return UgcPrefabCategory::props;
    if (has(roads, hash)) return UgcPrefabCategory::road_rail_and_bridges;
    if (has(signs, hash)) return UgcPrefabCategory::signs_and_banners;
    return std::nullopt;
}

std::string_view
ugc_prefab_category_localization_key(UgcPrefabCategory category) noexcept {
    switch (category) {
    case UgcPrefabCategory::landscape: return "UGC_CAT_LANDSCAPE";
    case UgcPrefabCategory::buildings_and_walls: return "UGC_CAT_BUILDINGSANDWALLS";
    case UgcPrefabCategory::nature: return "UGC_CAT_NATURE";
    case UgcPrefabCategory::props: return "UGC_CAT_PROPS";
    case UgcPrefabCategory::road_rail_and_bridges: return "UGC_CAT_ROADRAILANDBRIDGES";
    case UgcPrefabCategory::signs_and_banners: return "UGC_CAT_SIGNSANDBANNERS";
    }
    return "UGC_CAT_PROPS";
}

std::optional<std::string_view>
ugc_prefab_display_name(std::string_view prefab_name) noexcept {
    const auto hash = prefab_hash(prefab_name);
    const auto found = std::ranges::lower_bound(
        metadata, hash, {}, &PrefabMetadata::hash);
    if (found == metadata.end() || found->hash != hash) return std::nullopt;
    return found->display_name;
}

std::optional<std::string_view>
ugc_prefab_size_localization_key(std::string_view prefab_name) noexcept {
    const auto hash = prefab_hash(prefab_name);
    const auto found = std::ranges::lower_bound(
        metadata, hash, {}, &PrefabMetadata::hash);
    if (found == metadata.end() || found->hash != hash) return std::nullopt;
    return found->size_key;
}

} // namespace battlespades::frontend

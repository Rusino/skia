#include "SkIterators.h"

HBBlob stream_to_blob(std::unique_ptr<SkStreamAsset> asset) {
  size_t size = asset->getLength();
  HBBlob blob;
  if (const void* base = asset->getMemoryBase()) {
    blob.reset(hb_blob_create((char*) base, SkToUInt(size),
                              HB_MEMORY_MODE_READONLY, asset.release(),
                              [](void* p) { delete (SkStreamAsset*) p; }));
  } else {
    // SkDebugf("Extra SkStreamAsset copy\n");
    void* ptr = size ? sk_malloc_throw(size) : nullptr;
    asset->read(ptr, size);
    blob.reset(hb_blob_create((char*) ptr, SkToUInt(size),
                              HB_MEMORY_MODE_READONLY, ptr, sk_free));
  }
  SkASSERT(blob);
  hb_blob_make_immutable(blob.get());
  return blob;
}

HBFont create_hb_font(SkTypeface* tf) {
  if (!tf) {
    return nullptr;
  }
  int index;
  std::unique_ptr<SkStreamAsset> typefaceAsset(tf->openStream(&index));
  if (!typefaceAsset) {
    SkString name;
    tf->getFamilyName(&name);
    SkDebugf("Typeface '%s' has no data :(\n", name.c_str());
    return nullptr;
  }
  HBBlob blob(stream_to_blob(std::move(typefaceAsset)));
  HBFace face(hb_face_create(blob.get(), (unsigned) index));
  SkASSERT(face);
  if (!face) {
    return nullptr;
  }
  hb_face_set_index(face.get(), (unsigned) index);
  hb_face_set_upem(face.get(), tf->getUnitsPerEm());

  HBFont font(hb_font_create(face.get()));
  SkASSERT(font);
  if (!font) {
    return nullptr;
  }
  hb_ot_font_set_funcs(font.get());
  int axis_count = tf->getVariationDesignPosition(nullptr, 0);
  if (axis_count > 0) {
    SkAutoSTMalloc<4, SkFontArguments::VariationPosition::Coordinate>
        axis_values(axis_count);
    if (tf->getVariationDesignPosition(axis_values, axis_count) == axis_count) {
      hb_font_set_variations(font.get(),
                             reinterpret_cast<hb_variation_t*>(axis_values.get()),
                             axis_count);
    }
  }
  return font;
}

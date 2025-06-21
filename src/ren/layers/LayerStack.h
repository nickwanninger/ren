#pragma once

#include <ren/types.h>
#include <ren/layers/Layer.h>

namespace ren {


  // A layer stack is an ordered collection of layers.
  // Index 0 is the "bottom most", and index n is the "top most" layer.
  // Layers at the bottom are updated/rendered first, then the layers on top.
  // Layers receive events from top to bottom
  class LayerStack {
   public:
    LayerStack() = default;
    ~LayerStack() = default;

    inline void pushLayer(ref<Layer> layer) {
      layers.push_back(layer);
      layer->onAttach();
    }

    inline void popLayer(ref<Layer> layer) {
      auto it = std::find(layers.begin(), layers.end(), layer);
      if (it != layers.end()) {
        (*it)->onDetach();
        layers.erase(it);
      }
    }

    inline void clear(void) {
      // Detach all layers
      for (auto &layer : layers) {
        layer->onDetach();
      }
      layers.clear();
    }


    inline const std::vector<ref<Layer>> &getLayers(void) const { return layers; }

   private:
    std::vector<ref<Layer>> layers;
  };
}  // namespace ren
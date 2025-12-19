


struct PushConstants {
  float4x4 model, view, proj, normalMatrix;
};

PushConstant<PushConstants> pushConstants;
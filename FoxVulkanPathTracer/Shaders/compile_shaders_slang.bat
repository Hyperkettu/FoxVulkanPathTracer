slangc raygen.slang -stage raygeneration -entry main -profile spirv_1_4 -target spirv -o raygen.spv
slangc miss.slang -stage miss -entry main -profile spirv_1_4 -target spirv -o miss.spv
slangc closest_hit.slang -stage closesthit -entry main -profile spirv_1_4 -target spirv -o closestHit.spv


pause
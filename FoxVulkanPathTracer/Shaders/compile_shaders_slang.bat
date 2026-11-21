@echo off
rem Manual shader build. The Visual Studio project compiles these automatically
rem (see the CompileSlangShaders target in FoxVulkanPathTracer.vcxproj) - keep the two lists in sync.
rem Requires the Vulkan SDK, which provides slangc.
setlocal
cd /d "%~dp0"
set SLANGC="%VULKAN_SDK%\Bin\slangc.exe"
set OPTIONS=-entry main -profile spirv_1_5 -target spirv

rem Ray tracing pipeline
%SLANGC% raygen.slang -stage raygeneration %OPTIONS% -o raygen.spv || goto :error
rem Variant with shader execution reordering, used on GPUs that support it
%SLANGC% raygen.slang -stage raygeneration %OPTIONS% -DFOX_SER=1 -o raygen_ser.spv || goto :error
%SLANGC% miss.slang -stage miss %OPTIONS% -o miss.spv || goto :error
%SLANGC% shadowmiss.slang -stage miss %OPTIONS% -o shadowmiss.spv || goto :error
%SLANGC% closest_hit.slang -stage closesthit %OPTIONS% -o closest_hit.spv || goto :error
%SLANGC% any_hit.slang -stage anyhit %OPTIONS% -o any_hit.spv || goto :error

rem Tone mapping
%SLANGC% tonemapping_mesh.slang -stage mesh %OPTIONS% -o tonemapping_mesh.spv || goto :error
%SLANGC% tonemapping_frag.slang -stage fragment %OPTIONS% -o tonemapping_frag.spv || goto :error

rem Editor overlay (gizmos)
%SLANGC% overlay_vert.slang -stage vertex %OPTIONS% -o overlay_vert.spv || goto :error
%SLANGC% overlay_frag.slang -stage fragment %OPTIONS% -o overlay_frag.spv || goto :error

rem The hybrid path tracer's raster (cluster culling, visibility buffer, depth pyramid)
%SLANGC% hybrid_cull.slang -stage compute %OPTIONS% -DPASS=0 -o hybrid_cull_instances.spv || goto :error
%SLANGC% hybrid_cull.slang -stage compute %OPTIONS% -DPASS=1 -o hybrid_cull_clusters.spv || goto :error
%SLANGC% hybrid_cull.slang -stage compute %OPTIONS% -DPASS=2 -o hybrid_cull_candidates.spv || goto :error
%SLANGC% hybrid_cull.slang -stage compute %OPTIONS% -DPASS=3 -o hybrid_cull_large.spv || goto :error
%SLANGC% hybrid_hiz.slang -stage compute %OPTIONS% -o hybrid_hiz.spv || goto :error
%SLANGC% hybrid_swraster.slang -stage compute %OPTIONS% -o hybrid_swraster.spv || goto :error
%SLANGC% hybrid_resolve.slang -stage compute %OPTIONS% -o hybrid_resolve.spv || goto :error
%SLANGC% hybrid_raster_vert.slang -stage vertex %OPTIONS% -o hybrid_raster_vert.spv || goto :error
%SLANGC% hybrid_raster_frag.slang -stage fragment %OPTIONS% -o hybrid_raster_frag.spv || goto :error
%SLANGC% hybrid_raster_frag.slang -stage fragment %OPTIONS% -DMASKED=1 -o hybrid_raster_frag_masked.spv || goto :error

rem Skinning
%SLANGC% skinning.slang -stage compute %OPTIONS% -o skinning.spv || goto :error

rem Denoisers
for %%S in (accumulate svgf_temporal svgf_variance svgf_atrous svgf_modulate dlss_prepare) do (
    %SLANGC% %%S.slang -stage compute %OPTIONS% -o %%S.spv || goto :error
)

echo All shaders compiled.
exit /b 0

:error
echo Shader compilation failed.
exit /b 1

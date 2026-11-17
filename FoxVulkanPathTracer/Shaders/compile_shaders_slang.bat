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

rem Tone mapping
%SLANGC% tonemapping_mesh.slang -stage mesh %OPTIONS% -o tonemapping_mesh.spv || goto :error
%SLANGC% tonemapping_frag.slang -stage fragment %OPTIONS% -o tonemapping_frag.spv || goto :error

rem Editor overlay (gizmos)
%SLANGC% overlay_vert.slang -stage vertex %OPTIONS% -o overlay_vert.spv || goto :error
%SLANGC% overlay_frag.slang -stage fragment %OPTIONS% -o overlay_frag.spv || goto :error

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

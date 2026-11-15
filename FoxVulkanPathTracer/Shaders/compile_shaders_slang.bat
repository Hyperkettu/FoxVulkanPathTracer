@echo off
rem Manual shader build. The Visual Studio project compiles these automatically
rem (see the CompileSlangShaders target in FoxVulkanPathTracer.vcxproj) - keep the two lists in sync.
rem Requires the Vulkan SDK, which provides slangc.
setlocal
cd /d "%~dp0"
set SLANGC="%VULKAN_SDK%\Bin\slangc.exe"

%SLANGC% raygen.slang -stage raygeneration -entry main -profile spirv_1_5 -target spirv -o raygen.spv || goto :error
%SLANGC% miss.slang -stage miss -entry main -profile spirv_1_5 -target spirv -o miss.spv || goto :error
%SLANGC% shadowmiss.slang -stage miss -entry main -profile spirv_1_5 -target spirv -o shadowmiss.spv || goto :error
%SLANGC% closest_hit.slang -stage closesthit -entry main -profile spirv_1_5+spvRayQueryKHR -target spirv -o closest_hit.spv || goto :error
%SLANGC% tonemapping_mesh.slang -stage mesh -entry main -profile spirv_1_5 -target spirv -o tonemapping_mesh.spv || goto :error
%SLANGC% tonemapping_frag.slang -stage fragment -entry main -profile spirv_1_5 -target spirv -o tonemapping_frag.spv || goto :error

echo All shaders compiled.
exit /b 0

:error
echo Shader compilation failed.
exit /b 1

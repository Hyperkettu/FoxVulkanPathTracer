#pragma once

#include <vulkan/vulkan.h>

namespace Fox {
	
	namespace Graphics {
		
		namespace Enums {

			enum class PipelineShaderStage : int32_t { 
				VERTEX_SHADER = 0,
		        FRAGMENT_SHADER,
		        COMPUTE_SHADER,
		        MESH_SHADER,
		        TASK_SHADER
	        };				
		}

	}
}                

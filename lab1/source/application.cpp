#include "application.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>
#include <imgui.h>

namespace application { namespace {
constexpr unsigned max_objects=4;
constexpr float pi=3.14159265358979323846f;
struct Vertex { float position[3], color[3]; };
constexpr std::array<std::array<float,3>,6> vertex_positions{{
    {{1,0,0}},{{-1,0,0}},{{0,1,0}},{{0,-1,0}},{{0,0,1}},{{0,0,-1}}
}};
constexpr std::array<Vertex,6> makeVertices() {
    std::array<Vertex,6> result{};
    for(size_t i=0;i<result.size();++i)
        for(size_t axis=0;axis<3;++axis) {
            result[i].position[axis]=vertex_positions[i][axis];
            result[i].color[axis]=.5f*(vertex_positions[i][axis]+1.f);
        }
    return result;
}
constexpr auto vertices=makeVertices();
constexpr std::array<uint16_t,24> indices{{
    2,0,4, 2,4,1, 2,1,5, 2,5,0,
    3,4,0, 3,1,4, 3,5,1, 3,0,5
}};
struct Mat4 { float v[16]{}; };
Mat4 identity() { Mat4 m{}; m.v[0]=m.v[5]=m.v[10]=m.v[15]=1; return m; }
Mat4 mul(const Mat4& a,const Mat4& b) {
    Mat4 r{};
    for(int c=0;c<4;++c)
        for(int row=0;row<4;++row)
            for(int k=0;k<4;++k)
                r.v[c*4+row]+=a.v[k*4+row]*b.v[c*4+k];
    return r;
}
Mat4 translate(float x,float y,float z) { Mat4 m=identity(); m.v[12]=x;m.v[13]=y;m.v[14]=z;return m; }
Mat4 scale(float x,float y,float z) { Mat4 m=identity();m.v[0]=x;m.v[5]=y;m.v[10]=z;return m; }
Mat4 rotX(float a) { Mat4 m=identity();float c=std::cos(a),s=std::sin(a);m.v[5]=c;m.v[6]=s;m.v[9]=-s;m.v[10]=c;return m; }
Mat4 rotY(float a) { Mat4 m=identity();float c=std::cos(a),s=std::sin(a);m.v[0]=c;m.v[2]=-s;m.v[8]=s;m.v[10]=c;return m; }
Mat4 rotZ(float a) { Mat4 m=identity();float c=std::cos(a),s=std::sin(a);m.v[0]=c;m.v[1]=s;m.v[4]=-s;m.v[5]=c;return m; }
Mat4 perspective(float fov,float aspect) {
    Mat4 m{};float f=1.f/std::tan(fov*.5f);m.v[0]=f/aspect;m.v[5]=-f;
    m.v[10]=100.f/(.1f-100.f);m.v[11]=-1;m.v[14]=100.f*.1f/(.1f-100.f);return m;
}
Mat4 orthographic(float h,float aspect) {
    Mat4 m=identity();m.v[0]=1.f/(h*aspect);m.v[5]=-1.f/h;
    m.v[10]=1.f/(.1f-100.f);m.v[14]=.1f/(.1f-100.f);return m;
}
struct alignas(16) Uniform { Mat4 mvp; float tint[4]; };
static_assert(sizeof(Uniform)==80);
struct Buffer { VkBuffer handle=VK_NULL_HANDLE;VmaAllocation allocation=nullptr;void* mapped=nullptr; };
struct Object {
    float position[3]{},rotation[3]{},size[3]{1,1,1},tint[4]{1,1,1,1};
    Buffer uniform;VkDescriptorSet descriptor=VK_NULL_HANDLE;
};
struct State {
    Buffer vertex,index;std::array<Object,max_objects> objects{};unsigned count=1;
    VkDescriptorSetLayout descriptor_layout=VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool=VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout=VK_NULL_HANDLE;
    VkPipeline pipeline=VK_NULL_HANDLE;
    bool perspective=true,playing=true;
    float fov=55,ortho_height=3.5f,camera_distance=8;
    float speed=1,orbit_radius=.65f,vertical_amplitude=.35f;
    float orbit_frequency=1,vertical_frequency=2,spin_speed=90;
    double animation_time=0,previous_time=-1;
} s;
bool createBuffer(VkDeviceSize bytes,VkBufferUsageFlags usage,Buffer& out) {
    VkBufferCreateInfo bi{.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=bytes,.usage=usage,.sharingMode=VK_SHARING_MODE_EXCLUSIVE};
    VmaAllocationCreateInfo ai{.flags=VMA_ALLOCATION_CREATE_MAPPED_BIT|VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,.usage=VMA_MEMORY_USAGE_AUTO};
    VmaAllocationInfo mapped{};
    if(vmaCreateBuffer(graphics::internal::context.allocator,&bi,&ai,&out.handle,&out.allocation,&mapped)!=VK_SUCCESS) return false;
    out.mapped=mapped.pMappedData;return out.mapped!=nullptr;
}
void destroyBuffer(Buffer& b) { if(b.handle) vmaDestroyBuffer(graphics::internal::context.allocator,b.handle,b.allocation);b={}; }
void upload(Buffer& b,const void* data,size_t size) {
    memcpy(b.mapped,data,size);vmaFlushAllocation(graphics::internal::context.allocator,b.allocation,0,size);
}
VkShaderModule loadShader(const char* path) {
    std::ifstream f(path,std::ios::binary|std::ios::ate);if(!f) {std::cerr<<"Cannot open "<<path<<'\n';return VK_NULL_HANDLE;}
    std::streamoff n=f.tellg();if(n<=0 || n%4) return VK_NULL_HANDLE;
    std::vector<uint32_t> words(static_cast<size_t>(n)/4);f.seekg(0);f.read(reinterpret_cast<char*>(words.data()),n);
    if(!f)return VK_NULL_HANDLE;
    VkShaderModuleCreateInfo ci{.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=static_cast<size_t>(n),.pCode=words.data()};
    VkShaderModule result=VK_NULL_HANDLE;
    return vkCreateShaderModule(graphics::internal::context.device,&ci,nullptr,&result)==VK_SUCCESS?result:VK_NULL_HANDLE;
}
bool createPipeline() {
    auto device=graphics::internal::context.device;
    VkShaderModule v=loadShader("shaders/octahedron.vert.spv"),f=loadShader("shaders/octahedron.frag.spv");
    if(!v||!f) {if(v)vkDestroyShaderModule(device,v,nullptr);if(f)vkDestroyShaderModule(device,f,nullptr);return false;}
    VkPipelineShaderStageCreateInfo stages[2]{
        {.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_VERTEX_BIT,.module=v,.pName="main"},
        {.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_FRAGMENT_BIT,.module=f,.pName="main"}};
    VkVertexInputBindingDescription binding{0,sizeof(Vertex),VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attributes[2]{{0,0,VK_FORMAT_R32G32B32_SFLOAT,offsetof(Vertex,position)},
                                                    {1,0,VK_FORMAT_R32G32B32_SFLOAT,offsetof(Vertex,color)}};
    VkPipelineVertexInputStateCreateInfo vertex{.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount=1,.pVertexBindingDescriptions=&binding,.vertexAttributeDescriptionCount=2,.pVertexAttributeDescriptions=attributes};
    VkPipelineInputAssemblyStateCreateInfo assembly{.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkPipelineViewportStateCreateInfo viewport{.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,.viewportCount=1,.scissorCount=1};
    VkPipelineRasterizationStateCreateInfo raster{.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode=VK_POLYGON_MODE_FILL,.cullMode=VK_CULL_MODE_NONE,.frontFace=VK_FRONT_FACE_COUNTER_CLOCKWISE,.lineWidth=1};
    VkPipelineMultisampleStateCreateInfo multisample{.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT};
    VkPipelineDepthStencilStateCreateInfo depth{.sType=VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable=VK_TRUE,.depthWriteEnable=VK_TRUE,.depthCompareOp=VK_COMPARE_OP_LESS};
    VkPipelineColorBlendAttachmentState blendAttachment{.colorWriteMask=VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT};
    VkPipelineColorBlendStateCreateInfo blend{.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,.attachmentCount=1,.pAttachments=&blendAttachment};
    VkDynamicState dynamicStates[]{VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{.sType=VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,.dynamicStateCount=2,.pDynamicStates=dynamicStates};
    VkGraphicsPipelineCreateInfo info{.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,.stageCount=2,.pStages=stages,
        .pVertexInputState=&vertex,.pInputAssemblyState=&assembly,.pViewportState=&viewport,.pRasterizationState=&raster,
        .pMultisampleState=&multisample,.pDepthStencilState=&depth,.pColorBlendState=&blend,.pDynamicState=&dynamic,
        .layout=s.pipeline_layout,.renderPass=graphics::internal::context.render_pass};
    auto r=vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&info,nullptr,&s.pipeline);
    vkDestroyShaderModule(device,v,nullptr);vkDestroyShaderModule(device,f,nullptr);return r==VK_SUCCESS;
}
void reset(Object& o,unsigned i) {
    o.position[0]=i==1?-2.f:i==2?2.f:0;o.position[1]=i==3?-2.4f:0;o.position[2]=0;
    std::fill(o.rotation,o.rotation+3,0.f);std::fill(o.size,o.size+3,1.f);
    constexpr float colors[4][4]{{1,1,1,1},{1,.55f,.55f,1},{.55f,.8f,1,1},{.75f,1,.55f,1}};
    std::copy(colors[i],colors[i]+4,o.tint);
}
} // namespace

bool initialize() {
    auto device=graphics::internal::context.device;
    for(unsigned i=0;i<max_objects;++i)reset(s.objects[i],i);
    if(!createBuffer(sizeof(vertices),VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,s.vertex)||
       !createBuffer(sizeof(indices),VK_BUFFER_USAGE_INDEX_BUFFER_BIT,s.index))return false;
    upload(s.vertex,vertices.data(),sizeof(vertices));upload(s.index,indices.data(),sizeof(indices));
    for(auto& o:s.objects)if(!createBuffer(sizeof(Uniform),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,o.uniform))return false;
    VkDescriptorSetLayoutBinding binding{.binding=0,.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_VERTEX_BIT};
    VkDescriptorSetLayoutCreateInfo li{.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=1,.pBindings=&binding};
    if(vkCreateDescriptorSetLayout(device,&li,nullptr,&s.descriptor_layout)!=VK_SUCCESS)return false;
    VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,max_objects};
    VkDescriptorPoolCreateInfo pool_info{.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=max_objects,.poolSizeCount=1,.pPoolSizes=&ps};
    if(vkCreateDescriptorPool(device,&pool_info,nullptr,&s.descriptor_pool)!=VK_SUCCESS)return false;
    std::array<VkDescriptorSetLayout,max_objects> layouts{};layouts.fill(s.descriptor_layout);
    std::array<VkDescriptorSet,max_objects> sets{};
    VkDescriptorSetAllocateInfo ai{.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=s.descriptor_pool,
        .descriptorSetCount=max_objects,.pSetLayouts=layouts.data()};
    if(vkAllocateDescriptorSets(device,&ai,sets.data())!=VK_SUCCESS)return false;
    for(unsigned i=0;i<max_objects;++i) {
        auto& o=s.objects[i];o.descriptor=sets[i];VkDescriptorBufferInfo bi{o.uniform.handle,0,sizeof(Uniform)};
        VkWriteDescriptorSet write{.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=o.descriptor,.dstBinding=0,
            .descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,.pBufferInfo=&bi};
        vkUpdateDescriptorSets(device,1,&write,0,nullptr);
    }
    VkPipelineLayoutCreateInfo pli{.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,.pSetLayouts=&s.descriptor_layout};
    if(vkCreatePipelineLayout(device,&pli,nullptr,&s.pipeline_layout)!=VK_SUCCESS)return false;
    if(!createPipeline()){std::cerr<<"Failed to create pipeline\n";return false;}return true;
}
void shutdown() {
    auto device=graphics::internal::context.device;vkDeviceWaitIdle(device);
    if(s.pipeline)vkDestroyPipeline(device,s.pipeline,nullptr);
    if(s.pipeline_layout)vkDestroyPipelineLayout(device,s.pipeline_layout,nullptr);
    if(s.descriptor_pool)vkDestroyDescriptorPool(device,s.descriptor_pool,nullptr);
    if(s.descriptor_layout)vkDestroyDescriptorSetLayout(device,s.descriptor_layout,nullptr);
    for(auto& o:s.objects)destroyBuffer(o.uniform);
    destroyBuffer(s.index);destroyBuffer(s.vertex);
}
void update(double time) {
    if(s.previous_time>=0&&s.playing)s.animation_time+=std::clamp(time-s.previous_time,0.0,0.1)*s.speed;
    s.previous_time=time;
    ImGui::SetNextWindowPos(ImVec2(16,16),ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(480,650),ImGuiCond_FirstUseEver);
    ImGui::Begin("Lab 1 - Regular octahedron");
    ImGui::PushItemWidth(190.f);
    ImGui::Text("Variant 2 | 8 faces | 6 vertices");
    ImGui::SeparatorText("Projection");
    if(ImGui::RadioButton("Perspective",s.perspective))s.perspective=true;
    ImGui::SameLine();
    if(ImGui::RadioButton("Orthographic",!s.perspective))s.perspective=false;
    if(s.perspective)ImGui::SliderFloat("Field of view",&s.fov,20,100,"%.0f deg");
    else ImGui::SliderFloat("Ortho half height",&s.ortho_height,1.5f,8);
    if(s.perspective)ImGui::SliderFloat("Camera distance",&s.camera_distance,5,15);
    else ImGui::TextDisabled("Camera distance: perspective only");
    ImGui::SeparatorText("Trajectory and animation");
    if(ImGui::Button(s.playing?"Pause":"Play"))s.playing=!s.playing;
    ImGui::SameLine();if(ImGui::Button("Restart"))s.animation_time=0;
    ImGui::SliderFloat("Speed",&s.speed,0,4);
    ImGui::SliderFloat("Orbit radius",&s.orbit_radius,0,2);
    ImGui::SliderFloat("Orbit frequency",&s.orbit_frequency,0,4);
    ImGui::SliderFloat("Vertical amplitude",&s.vertical_amplitude,0,2);
    ImGui::SliderFloat("Vertical frequency",&s.vertical_frequency,0,5);
    ImGui::SliderFloat("Spin speed",&s.spin_speed,-360,360,"%.0f deg/s");
    ImGui::Text("Animation time %.2f s",s.animation_time);
    ImGui::SeparatorText("Objects and colours");
    int count=static_cast<int>(s.count);if(ImGui::SliderInt("Object count",&count,1,max_objects))s.count=static_cast<unsigned>(count);
    ImGui::TextWrapped("Tint multiplies procedural vertex colours from local position.");
    for(unsigned i=0;i<s.count;++i) {
        ImGui::PushID(static_cast<int>(i));char label[32];std::snprintf(label,sizeof(label),"Octahedron %u",i+1);
        if(ImGui::TreeNode(label)) {
            auto& o=s.objects[i];ImGui::DragFloat3("Position",o.position,.02f,-8,8);
            ImGui::DragFloat3("Rotation (deg)",o.rotation,.5f,-360,360);
            ImGui::DragFloat3("Scale",o.size,.01f,.15f,3);
            for(float& v:o.size)v=std::clamp(v,.15f,3.f);
            ImGui::ColorEdit3("Tint",o.tint);
            if(ImGui::Button("Reset object"))reset(o,i);
            ImGui::TreePop();
        }ImGui::PopID();
    }
    ImGui::Separator();ImGui::Text("FPS %.1f",ImGui::GetIO().Framerate);
    ImGui::PopItemWidth();ImGui::End();
}
void render(const graphics::internal::FrameData& fd) {
    auto& c=graphics::internal::context;auto cmd=fd.command_buffer;if(!cmd||!fd.framebuffer)return;
    vkResetCommandBuffer(cmd,0);
    VkCommandBufferBeginInfo begin{.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    vkBeginCommandBuffer(cmd,&begin);
    VkClearValue clear[2]{};clear[0].color={{.045f,.055f,.09f,1}};clear[1].depthStencil={1,0};
    VkRenderPassBeginInfo pass{.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,.renderPass=c.render_pass,
        .framebuffer=fd.framebuffer,.renderArea={{0,0},c.swapchain_extent},.clearValueCount=2,.pClearValues=clear};
    vkCmdBeginRenderPass(cmd,&pass,VK_SUBPASS_CONTENTS_INLINE);
    VkViewport viewport{0,0,static_cast<float>(c.swapchain_extent.width),static_cast<float>(c.swapchain_extent.height),0,1};
    VkRect2D scissor{{0,0},c.swapchain_extent};vkCmdSetViewport(cmd,0,1,&viewport);vkCmdSetScissor(cmd,0,1,&scissor);
    vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,s.pipeline);
    VkDeviceSize offset=0;vkCmdBindVertexBuffers(cmd,0,1,&s.vertex.handle,&offset);
    vkCmdBindIndexBuffer(cmd,s.index.handle,0,VK_INDEX_TYPE_UINT16);
    float aspect=static_cast<float>(c.swapchain_extent.width)/std::max(1u,c.swapchain_extent.height);
    Mat4 projection=s.perspective?perspective(s.fov*pi/180,aspect):orthographic(s.ortho_height,aspect);
    // Shift the scene into the unobscured part of the window beside the controls.
    Mat4 view=translate(2.2f,0,-s.camera_distance);
    for(unsigned i=0;i<s.count;++i) {
        auto& o=s.objects[i];float t=static_cast<float>(s.animation_time),phase=t*s.orbit_frequency+i*1.7f;
        float x=o.position[0]+s.orbit_radius*(std::cos(phase)-std::cos(i*1.7f));
        float y=o.position[1]+s.vertical_amplitude*std::sin(t*s.vertical_frequency+i*1.3f);
        float z=o.position[2]+s.orbit_radius*std::sin(phase);
        float rx=o.rotation[0]*pi/180,ry=(o.rotation[1]+t*s.spin_speed)*pi/180,rz=o.rotation[2]*pi/180;
        Mat4 model=mul(translate(x,y,z),mul(rotZ(rz),mul(rotY(ry),mul(rotX(rx),scale(o.size[0],o.size[1],o.size[2])))));
        Uniform u{mul(projection,mul(view,model)),{o.tint[0],o.tint[1],o.tint[2],1}};
        upload(o.uniform,&u,sizeof(u));
        vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,s.pipeline_layout,0,1,&o.descriptor,0,nullptr);
        vkCmdDrawIndexed(cmd,static_cast<uint32_t>(indices.size()),1,0,0,0);
    }
    vkCmdEndRenderPass(cmd);vkEndCommandBuffer(cmd);
}
} // namespace application

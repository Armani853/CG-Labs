#include "application.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

#include <imgui.h>

namespace application {
namespace {

constexpr float pi = 3.14159265358979323846f;
constexpr unsigned max_objects = 4;
constexpr unsigned max_lights = 8;
constexpr unsigned model_slots = max_objects + 2 * max_lights;
constexpr unsigned cone_segments = 24;
constexpr unsigned cone_vertices = 2 * cone_segments + 8;

struct Vec3 { float x, y, z; };
Vec3 operator+(Vec3 a, Vec3 b) { return {a.x+b.x, a.y+b.y, a.z+b.z}; }
Vec3 operator-(Vec3 a, Vec3 b) { return {a.x-b.x, a.y-b.y, a.z-b.z}; }
Vec3 operator*(Vec3 a, float s) { return {a.x*s, a.y*s, a.z*s}; }
Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x};
}
float length(Vec3 a) { return std::sqrt(a.x*a.x+a.y*a.y+a.z*a.z); }
Vec3 normalized(Vec3 a, Vec3 fallback={0,-1,0}) {
    float n=length(a); return n>0.0001f ? a*(1.f/n) : fallback;
}

struct Vertex { float position[3], normal[3]; };
Vertex makeVertex(Vec3 p, Vec3 n) { return {{p.x,p.y,p.z},{n.x,n.y,n.z}}; }
constexpr std::array<Vec3,6> corners{{
    {1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}
}};
constexpr std::array<std::array<unsigned,3>,8> faces{{
    {{2,0,4}},{{2,4,1}},{{2,1,5}},{{2,5,0}},
    {{3,4,0}},{{3,1,4}},{{3,5,1}},{{3,0,5}}
}};
std::array<Vertex,24> makeOctahedron() {
    std::array<Vertex,24> result{};
    for(unsigned f=0; f<faces.size(); ++f) {
        Vec3 a=corners[faces[f][0]], b=corners[faces[f][1]], c=corners[faces[f][2]];
        Vec3 normal=normalized(cross(c-a,b-a));
        result[3*f]=makeVertex(a,normal);
        result[3*f+1]=makeVertex(b,normal);
        result[3*f+2]=makeVertex(c,normal);
    }
    return result;
}
std::array<Vertex,36> makeMarkerCube() {
    std::array<Vertex,36> result{};
    constexpr float h=.13f;
    constexpr std::array<Vec3,8> p{{
        {-h,-h,-h},{h,-h,-h},{h,h,-h},{-h,h,-h},
        {-h,-h,h},{h,-h,h},{h,h,h},{-h,h,h}
    }};
    constexpr std::array<std::array<unsigned,3>,12> triangles{{
        {{4,5,6}},{{4,6,7}},{{1,0,3}},{{1,3,2}},
        {{0,4,7}},{{0,7,3}},{{5,1,2}},{{5,2,6}},
        {{7,6,2}},{{7,2,3}},{{0,1,5}},{{0,5,4}}
    }};
    for(unsigned i=0;i<triangles.size();++i)
        for(unsigned j=0;j<3;++j) result[3*i+j]=makeVertex(p[triangles[i][j]],{0,1,0});
    return result;
}

struct Mat4 { float v[16]{}; };
Mat4 identity() { Mat4 m{}; m.v[0]=m.v[5]=m.v[10]=m.v[15]=1; return m; }
Mat4 mul(const Mat4& a,const Mat4& b) {
    Mat4 r{};
    for(int c=0;c<4;++c) for(int row=0;row<4;++row)
        for(int k=0;k<4;++k) r.v[c*4+row]+=a.v[k*4+row]*b.v[c*4+k];
    return r;
}
Mat4 translate(Vec3 p) { Mat4 m=identity();m.v[12]=p.x;m.v[13]=p.y;m.v[14]=p.z;return m; }
Mat4 scale(Vec3 p) { Mat4 m=identity();m.v[0]=p.x;m.v[5]=p.y;m.v[10]=p.z;return m; }
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

struct alignas(16) SceneGPU {
    Mat4 viewProjection;
    float cameraPosition[4];
    float ambient[4];
    float directionalDirection[4];
    float directionalColor[4];
    int counts[4];
};
struct alignas(16) ModelGPU {
    Mat4 model;
    Mat4 normalMatrix;
    float albedo[4];
    float specular[4];
};
struct alignas(16) PointGPU { float position[4], colorIntensity[4]; };
struct alignas(16) SpotGPU { float position[4], directionInner[4], colorIntensity[4], outerCos[4]; };
static_assert(sizeof(SceneGPU)==144 && sizeof(ModelGPU)==160);
static_assert(sizeof(PointGPU)==32 && sizeof(SpotGPU)==64);

struct Buffer { VkBuffer handle=VK_NULL_HANDLE; VmaAllocation allocation=nullptr; void* mapped=nullptr; };
struct ModelSlot { Buffer uniform; VkDescriptorSet descriptor=VK_NULL_HANDLE; };
struct Object {
    float position[3]{}, rotation[3]{}, size[3]{1,1,1};
    float albedo[3]{.6f,.72f,1.f};
    float specular[3]{1,1,1};
    float shininess=48;
};
struct Light {
    float position[3]{}, direction[3]{0,-1,0}, color[3]{1,1,1};
    float intensity=8, innerAngle=15, outerAngle=25;
};
struct State {
    Buffer octahedron, cube, cones, sceneUniform, pointStorage, spotStorage;
    std::array<ModelSlot,model_slots> models{};
    std::array<Object,max_objects> objects{};
    std::array<Light,max_lights> points{},spots{};
    unsigned objectCount=1,pointCount=2,spotCount=1;
    VkDescriptorSetLayout sceneLayout=VK_NULL_HANDLE,modelLayout=VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool=VK_NULL_HANDLE;
    VkDescriptorSet sceneDescriptor=VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout=VK_NULL_HANDLE;
    VkPipeline litPipeline=VK_NULL_HANDLE,markerPipeline=VK_NULL_HANDLE,linePipeline=VK_NULL_HANDLE;
    float cameraPosition[3]{-2.4f,1.4f,8.f};
    float cameraRotation[3]{-8.f,0,0};
    float fov=55;
    float ambientColor[3]{.22f,.27f,.4f},ambientIntensity=.45f;
    float sunDirection[3]{-.5f,-1.f,-.7f},sunColor[3]{1.f,.95f,.82f},sunIntensity=1.4f;
    bool showMarkers=true,animate=false;
    float animationSpeed=1,orbitRadius=.35f;
    double animationTime=0,previousTime=-1;
} s;

bool createBuffer(VkDeviceSize bytes,VkBufferUsageFlags usage,Buffer& out) {
    VkBufferCreateInfo bi{.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=bytes,
        .usage=usage,.sharingMode=VK_SHARING_MODE_EXCLUSIVE};
    VmaAllocationCreateInfo ai{.flags=VMA_ALLOCATION_CREATE_MAPPED_BIT|
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,.usage=VMA_MEMORY_USAGE_AUTO};
    VmaAllocationInfo allocation{};
    if(vmaCreateBuffer(graphics::internal::context.allocator,&bi,&ai,
                       &out.handle,&out.allocation,&allocation)!=VK_SUCCESS) return false;
    out.mapped=allocation.pMappedData;
    return out.mapped!=nullptr;
}
void destroyBuffer(Buffer& b) {
    if(b.handle) vmaDestroyBuffer(graphics::internal::context.allocator,b.handle,b.allocation);
    b={};
}
void upload(Buffer& b,const void* data,size_t size) {
    std::memcpy(b.mapped,data,size);
    vmaFlushAllocation(graphics::internal::context.allocator,b.allocation,0,size);
}
VkShaderModule loadShader(const char* path) {
    std::ifstream file(path,std::ios::binary|std::ios::ate);
    if(!file){std::cerr<<"Cannot open "<<path<<'\n';return VK_NULL_HANDLE;}
    std::streamoff bytes=file.tellg();
    if(bytes<=0 || bytes%4) return VK_NULL_HANDLE;
    std::vector<uint32_t> code(static_cast<size_t>(bytes)/4);
    file.seekg(0);file.read(reinterpret_cast<char*>(code.data()),bytes);
    if(!file) return VK_NULL_HANDLE;
    VkShaderModuleCreateInfo ci{.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize=static_cast<size_t>(bytes),.pCode=code.data()};
    VkShaderModule module=VK_NULL_HANDLE;
    return vkCreateShaderModule(graphics::internal::context.device,&ci,nullptr,&module)==VK_SUCCESS
        ? module : VK_NULL_HANDLE;
}

bool createPipeline(VkShaderModule vertex,VkShaderModule fragment,
                    VkPrimitiveTopology topology,VkPipeline& pipeline) {
    auto device=graphics::internal::context.device;
    VkPipelineShaderStageCreateInfo stages[2]{
        {.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage=VK_SHADER_STAGE_VERTEX_BIT,.module=vertex,.pName="main"},
        {.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage=VK_SHADER_STAGE_FRAGMENT_BIT,.module=fragment,.pName="main"}};
    VkVertexInputBindingDescription binding{0,sizeof(Vertex),VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attributes[2]{
        {0,0,VK_FORMAT_R32G32B32_SFLOAT,offsetof(Vertex,position)},
        {1,0,VK_FORMAT_R32G32B32_SFLOAT,offsetof(Vertex,normal)}};
    VkPipelineVertexInputStateCreateInfo vertexInput{.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount=1,.pVertexBindingDescriptions=&binding,
        .vertexAttributeDescriptionCount=2,.pVertexAttributeDescriptions=attributes};
    VkPipelineInputAssemblyStateCreateInfo assembly{.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology=topology};
    VkPipelineViewportStateCreateInfo viewport{.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount=1,.scissorCount=1};
    VkPipelineRasterizationStateCreateInfo raster{.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode=VK_POLYGON_MODE_FILL,.cullMode=VK_CULL_MODE_NONE,
        .frontFace=VK_FRONT_FACE_COUNTER_CLOCKWISE,.lineWidth=1};
    VkPipelineMultisampleStateCreateInfo multisample{.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples=VK_SAMPLE_COUNT_1_BIT};
    VkPipelineDepthStencilStateCreateInfo depth{.sType=VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable=VK_TRUE,.depthWriteEnable=VK_TRUE,.depthCompareOp=VK_COMPARE_OP_LESS};
    VkPipelineColorBlendAttachmentState blendAttachment{.colorWriteMask=VK_COLOR_COMPONENT_R_BIT|
        VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT};
    VkPipelineColorBlendStateCreateInfo blend{.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount=1,.pAttachments=&blendAttachment};
    VkDynamicState dynamicStates[]{VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{.sType=VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount=2,.pDynamicStates=dynamicStates};
    VkGraphicsPipelineCreateInfo ci{.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount=2,.pStages=stages,.pVertexInputState=&vertexInput,
        .pInputAssemblyState=&assembly,.pViewportState=&viewport,
        .pRasterizationState=&raster,.pMultisampleState=&multisample,
        .pDepthStencilState=&depth,.pColorBlendState=&blend,.pDynamicState=&dynamic,
        .layout=s.pipelineLayout,.renderPass=graphics::internal::context.render_pass};
    return vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&ci,nullptr,&pipeline)==VK_SUCCESS;
}
bool createPipelines() {
    auto device=graphics::internal::context.device;
    VkShaderModule vertex=loadShader("shaders/lit.vert.spv");
    VkShaderModule lit=loadShader("shaders/lit.frag.spv");
    VkShaderModule unlit=loadShader("shaders/unlit.frag.spv");
    bool ok=vertex&&lit&&unlit;
    if(ok) ok=createPipeline(vertex,lit,VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,s.litPipeline)
        &&createPipeline(vertex,unlit,VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,s.markerPipeline)
        &&createPipeline(vertex,unlit,VK_PRIMITIVE_TOPOLOGY_LINE_LIST,s.linePipeline);
    if(vertex)vkDestroyShaderModule(device,vertex,nullptr);
    if(lit)vkDestroyShaderModule(device,lit,nullptr);
    if(unlit)vkDestroyShaderModule(device,unlit,nullptr);
    return ok;
}

void setModel(unsigned slot,const Mat4& model,const Mat4& normalMatrix,
              const float* color,const float* specular,float shininess) {
    ModelGPU gpu{};gpu.model=model;gpu.normalMatrix=normalMatrix;
    for(int i=0;i<3;++i){gpu.albedo[i]=color[i];gpu.specular[i]=specular[i];}
    gpu.albedo[3]=1;gpu.specular[3]=shininess;
    upload(s.models[slot].uniform,&gpu,sizeof(gpu));
}
Vec3 lightDirection(const Light& l) {
    return normalized({l.direction[0],l.direction[1],l.direction[2]});
}
std::array<Vertex,cone_vertices> makeCone(const Light& light) {
    std::array<Vertex,cone_vertices> result{};
    Vec3 direction=lightDirection(light);
    Vec3 reference=std::abs(direction.y)<.9f ? Vec3{0,1,0} : Vec3{1,0,0};
    Vec3 u=normalized(cross(direction,reference));
    Vec3 v=cross(direction,u);
    float radius=2.f*std::tan(light.outerAngle*pi/180.f);
    auto ring=[&](unsigned i) {
        float angle=2*pi*static_cast<float>(i)/cone_segments;
        return direction*2.f+(u*std::cos(angle)+v*std::sin(angle))*radius;
    };
    for(unsigned i=0;i<cone_segments;++i) {
        result[2*i]=makeVertex(ring(i),{0,1,0});
        result[2*i+1]=makeVertex(ring((i+1)%cone_segments),{0,1,0});
    }
    for(unsigned i=0;i<4;++i) {
        result[2*cone_segments+2*i]=makeVertex({0,0,0},{0,1,0});
        result[2*cone_segments+2*i+1]=makeVertex(ring(i*cone_segments/4),{0,1,0});
    }
    return result;
}
void resetCamera() {
    const float p[3]{-2.4f,1.4f,8.f},r[3]{-8.f,0,0};
    std::copy(p,p+3,s.cameraPosition);std::copy(r,r+3,s.cameraRotation);s.fov=55;
}
void initialValues() {
    for(unsigned i=0;i<max_objects;++i) {
        s.objects[i].position[0]=i==1?-2.f:i==2?2.f:0;
        s.objects[i].position[1]=i==3?2.f:0;
    }
    s.points[0].position[0]=-1.5f;s.points[0].position[1]=1.5f;s.points[0].position[2]=2;
    s.points[0].color[0]=1;s.points[0].color[1]=.35f;s.points[0].color[2]=.2f;
    s.points[1].position[0]=2;s.points[1].position[1]=.5f;s.points[1].position[2]=1;
    s.points[1].color[0]=.25f;s.points[1].color[1]=.5f;s.points[1].color[2]=1;
    s.spots[0].position[0]=0;s.spots[0].position[1]=3;s.spots[0].position[2]=1;
    s.spots[0].direction[0]=0;s.spots[0].direction[1]=-1;s.spots[0].direction[2]=-.25f;
    s.spots[0].intensity=15;
}

} // namespace

bool initialize() {
    auto device=graphics::internal::context.device;
    initialValues();
    auto octahedron=makeOctahedron();auto cube=makeMarkerCube();
    if(!createBuffer(sizeof(octahedron),VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,s.octahedron) ||
       !createBuffer(sizeof(cube),VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,s.cube) ||
       !createBuffer(sizeof(Vertex)*cone_vertices*max_lights,VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,s.cones) ||
       !createBuffer(sizeof(SceneGPU),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,s.sceneUniform) ||
       !createBuffer(sizeof(PointGPU)*max_lights,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,s.pointStorage) ||
       !createBuffer(sizeof(SpotGPU)*max_lights,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,s.spotStorage)) return false;
    upload(s.octahedron,octahedron.data(),sizeof(octahedron));
    upload(s.cube,cube.data(),sizeof(cube));
    for(auto& slot:s.models)
        if(!createBuffer(sizeof(ModelGPU),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,slot.uniform)) return false;

    VkDescriptorSetLayoutBinding sceneBindings[3]{
        {0,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,nullptr},
        {1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr},
        {2,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr}};
    VkDescriptorSetLayoutCreateInfo sceneInfo{.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount=3,.pBindings=sceneBindings};
    if(vkCreateDescriptorSetLayout(device,&sceneInfo,nullptr,&s.sceneLayout)!=VK_SUCCESS) return false;
    VkDescriptorSetLayoutBinding modelBinding{0,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,
        VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,nullptr};
    VkDescriptorSetLayoutCreateInfo modelInfo{.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount=1,.pBindings=&modelBinding};
    if(vkCreateDescriptorSetLayout(device,&modelInfo,nullptr,&s.modelLayout)!=VK_SUCCESS) return false;
    VkDescriptorPoolSize poolSizes[2]{{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,model_slots+1},
                                      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,2}};
    VkDescriptorPoolCreateInfo poolInfo{.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets=model_slots+1,.poolSizeCount=2,.pPoolSizes=poolSizes};
    if(vkCreateDescriptorPool(device,&poolInfo,nullptr,&s.descriptorPool)!=VK_SUCCESS) return false;
    VkDescriptorSetAllocateInfo sceneAllocate{.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool=s.descriptorPool,.descriptorSetCount=1,.pSetLayouts=&s.sceneLayout};
    if(vkAllocateDescriptorSets(device,&sceneAllocate,&s.sceneDescriptor)!=VK_SUCCESS) return false;
    std::array<VkDescriptorSetLayout,model_slots> layouts{};layouts.fill(s.modelLayout);
    std::array<VkDescriptorSet,model_slots> sets{};
    VkDescriptorSetAllocateInfo modelAllocate{.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool=s.descriptorPool,.descriptorSetCount=model_slots,.pSetLayouts=layouts.data()};
    if(vkAllocateDescriptorSets(device,&modelAllocate,sets.data())!=VK_SUCCESS) return false;
    VkDescriptorBufferInfo sceneBuffers[3]{{s.sceneUniform.handle,0,sizeof(SceneGPU)},
        {s.pointStorage.handle,0,sizeof(PointGPU)*max_lights},
        {s.spotStorage.handle,0,sizeof(SpotGPU)*max_lights}};
    VkWriteDescriptorSet sceneWrites[3]{};
    for(unsigned i=0;i<3;++i) {
        sceneWrites[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        sceneWrites[i].dstSet=s.sceneDescriptor;sceneWrites[i].dstBinding=i;
        sceneWrites[i].descriptorCount=1;
        sceneWrites[i].descriptorType=i==0?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        sceneWrites[i].pBufferInfo=&sceneBuffers[i];
    }
    vkUpdateDescriptorSets(device,3,sceneWrites,0,nullptr);
    for(unsigned i=0;i<model_slots;++i) {
        s.models[i].descriptor=sets[i];
        VkDescriptorBufferInfo buffer{s.models[i].uniform.handle,0,sizeof(ModelGPU)};
        VkWriteDescriptorSet write{.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet=sets[i],.dstBinding=0,.descriptorCount=1,
            .descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,.pBufferInfo=&buffer};
        vkUpdateDescriptorSets(device,1,&write,0,nullptr);
    }
    VkDescriptorSetLayout pipelineSets[2]{s.sceneLayout,s.modelLayout};
    VkPipelineLayoutCreateInfo layoutInfo{.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount=2,.pSetLayouts=pipelineSets};
    if(vkCreatePipelineLayout(device,&layoutInfo,nullptr,&s.pipelineLayout)!=VK_SUCCESS) return false;
    if(!createPipelines()){std::cerr<<"Failed to create Lab 2 pipelines\n";return false;}
    return true;
}

void shutdown() {
    auto device=graphics::internal::context.device;
    vkDeviceWaitIdle(device);
    if(s.linePipeline)vkDestroyPipeline(device,s.linePipeline,nullptr);
    if(s.markerPipeline)vkDestroyPipeline(device,s.markerPipeline,nullptr);
    if(s.litPipeline)vkDestroyPipeline(device,s.litPipeline,nullptr);
    if(s.pipelineLayout)vkDestroyPipelineLayout(device,s.pipelineLayout,nullptr);
    if(s.descriptorPool)vkDestroyDescriptorPool(device,s.descriptorPool,nullptr);
    if(s.modelLayout)vkDestroyDescriptorSetLayout(device,s.modelLayout,nullptr);
    if(s.sceneLayout)vkDestroyDescriptorSetLayout(device,s.sceneLayout,nullptr);
    for(auto& slot:s.models)destroyBuffer(slot.uniform);
    destroyBuffer(s.spotStorage);destroyBuffer(s.pointStorage);destroyBuffer(s.sceneUniform);
    destroyBuffer(s.cones);destroyBuffer(s.cube);destroyBuffer(s.octahedron);
}

void update(double time) {
    if(s.previousTime>=0 && s.animate)
        s.animationTime+=std::clamp(time-s.previousTime,0.0,0.1)*s.animationSpeed;
    s.previousTime=time;
    ImGui::SetNextWindowPos(ImVec2(16,16),ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(440,670),ImGuiCond_FirstUseEver);
    ImGui::Begin("Lab 2 - Camera and lighting");
    ImGui::PushItemWidth(170);
    ImGui::Text("Variant 2: regular octahedron");
    ImGui::Text("FPS %.1f",ImGui::GetIO().Framerate);
    if(ImGui::CollapsingHeader("Camera",ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat3("Camera position",s.cameraPosition,.03f,-20,20);
        ImGui::DragFloat3("Camera rotation",s.cameraRotation,.3f,-180,180,"%.1f deg");
        ImGui::SliderFloat("Field of view",&s.fov,20,100,"%.0f deg");
        if(ImGui::Button("Reset camera"))resetCamera();
    }
    if(ImGui::CollapsingHeader("Directional and ambient",ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::ColorEdit3("Ambient colour",s.ambientColor);
        ImGui::SliderFloat("Ambient intensity",&s.ambientIntensity,0,2);
        ImGui::DragFloat3("Light direction",s.sunDirection,.02f,-2,2);
        ImGui::ColorEdit3("Directional colour",s.sunColor);
        ImGui::SliderFloat("Directional intensity",&s.sunIntensity,0,6);
    }
    if(ImGui::CollapsingHeader("Point lights",ImGuiTreeNodeFlags_DefaultOpen)) {
        if(s.pointCount<max_lights && ImGui::Button("Add point light")) {
            s.points[s.pointCount]=Light{};
            s.points[s.pointCount].position[0]=static_cast<float>(s.pointCount)-1;
            s.points[s.pointCount].position[1]=1.5f;
            ++s.pointCount;
        }
        int remove=-1;
        for(unsigned i=0;i<s.pointCount;++i) {
            ImGui::PushID(static_cast<int>(i));char name[32];std::snprintf(name,sizeof(name),"Point %u",i+1);
            if(ImGui::TreeNode(name)) {
                auto& light=s.points[i];
                ImGui::DragFloat3("Position",light.position,.03f,-15,15);
                ImGui::ColorEdit3("Colour",light.color);
                ImGui::SliderFloat("Intensity",&light.intensity,0,30);
                if(ImGui::Button("Remove point"))remove=static_cast<int>(i);
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        if(remove>=0) {
            for(unsigned i=static_cast<unsigned>(remove);i+1<s.pointCount;++i)s.points[i]=s.points[i+1];
            --s.pointCount;
        }
    }
    if(ImGui::CollapsingHeader("Spot lights",ImGuiTreeNodeFlags_DefaultOpen)) {
        if(s.spotCount<max_lights && ImGui::Button("Add spot light")) {
            s.spots[s.spotCount]=Light{};
            s.spots[s.spotCount].position[1]=3;
            s.spots[s.spotCount].intensity=15;
            ++s.spotCount;
        }
        int remove=-1;
        for(unsigned i=0;i<s.spotCount;++i) {
            ImGui::PushID(static_cast<int>(i)+100);char name[32];std::snprintf(name,sizeof(name),"Spot %u",i+1);
            if(ImGui::TreeNode(name)) {
                auto& light=s.spots[i];
                ImGui::DragFloat3("Position",light.position,.03f,-15,15);
                ImGui::DragFloat3("Direction",light.direction,.02f,-2,2);
                ImGui::ColorEdit3("Colour",light.color);
                ImGui::SliderFloat("Intensity",&light.intensity,0,40);
                ImGui::SliderFloat("Inner angle",&light.innerAngle,1,65,"%.0f deg");
                ImGui::SliderFloat("Outer angle",&light.outerAngle,1,75,"%.0f deg");
                light.outerAngle=std::max(light.outerAngle,light.innerAngle);
                if(ImGui::Button("Remove spot"))remove=static_cast<int>(i);
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        if(remove>=0) {
            for(unsigned i=static_cast<unsigned>(remove);i+1<s.spotCount;++i)s.spots[i]=s.spots[i+1];
            --s.spotCount;
        }
    }
    if(ImGui::CollapsingHeader("Objects and material",ImGuiTreeNodeFlags_DefaultOpen)) {
        int count=static_cast<int>(s.objectCount);
        if(ImGui::SliderInt("Object count",&count,1,max_objects))s.objectCount=static_cast<unsigned>(count);
        ImGui::Checkbox("Animate objects",&s.animate);
        if(s.animate) {
            ImGui::SliderFloat("Animation speed",&s.animationSpeed,0,4);
            ImGui::SliderFloat("Orbit radius",&s.orbitRadius,0,2);
        }
        for(unsigned i=0;i<s.objectCount;++i) {
            ImGui::PushID(static_cast<int>(i)+200);char name[32];std::snprintf(name,sizeof(name),"Octahedron %u",i+1);
            if(ImGui::TreeNode(name)) {
                auto& object=s.objects[i];
                ImGui::DragFloat3("Position",object.position,.03f,-8,8);
                ImGui::DragFloat3("Rotation",object.rotation,.4f,-360,360,"%.1f deg");
                ImGui::DragFloat3("Scale",object.size,.02f,.15f,4);
                for(float& v:object.size)v=std::clamp(v,.15f,4.f);
                ImGui::ColorEdit3("Albedo",object.albedo);
                ImGui::ColorEdit3("Specular colour",object.specular);
                ImGui::SliderFloat("Shininess",&object.shininess,1,128);
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    }
    ImGui::Checkbox("Show light markers and cones",&s.showMarkers);
    ImGui::PopItemWidth();ImGui::End();
}

void render(const graphics::internal::FrameData& fd) {
    auto& context=graphics::internal::context;
    VkCommandBuffer cmd=fd.command_buffer;
    if(!cmd || !fd.framebuffer)return;
    // One copy of each mapped buffer is shared across swapchain frames.
    // Wait before changing it so no earlier draw can still read those bytes.
    vkDeviceWaitIdle(context.device);
    float aspect=static_cast<float>(context.swapchain_extent.width)/
                 std::max(1u,context.swapchain_extent.height);
    Mat4 projection=perspective(s.fov*pi/180.f,aspect);
    Mat4 view=mul(rotZ(-s.cameraRotation[2]*pi/180.f),
        mul(rotX(-s.cameraRotation[0]*pi/180.f),
        mul(rotY(-s.cameraRotation[1]*pi/180.f),
        translate({-s.cameraPosition[0],-s.cameraPosition[1],-s.cameraPosition[2]}))));
    SceneGPU scene{};
    scene.viewProjection=mul(projection,view);
    std::copy(s.cameraPosition,s.cameraPosition+3,scene.cameraPosition);
    std::copy(s.ambientColor,s.ambientColor+3,scene.ambient);
    scene.ambient[3]=s.ambientIntensity;
    Vec3 sun=normalized({s.sunDirection[0],s.sunDirection[1],s.sunDirection[2]});
    scene.directionalDirection[0]=sun.x;scene.directionalDirection[1]=sun.y;
    scene.directionalDirection[2]=sun.z;
    std::copy(s.sunColor,s.sunColor+3,scene.directionalColor);
    scene.directionalColor[3]=s.sunIntensity;
    scene.counts[0]=static_cast<int>(s.pointCount);
    scene.counts[1]=static_cast<int>(s.spotCount);
    upload(s.sceneUniform,&scene,sizeof(scene));
    std::array<PointGPU,max_lights> pointData{};
    for(unsigned i=0;i<s.pointCount;++i) {
        auto& source=s.points[i];auto& target=pointData[i];
        std::copy(source.position,source.position+3,target.position);
        std::copy(source.color,source.color+3,target.colorIntensity);
        target.colorIntensity[3]=source.intensity;
    }
    upload(s.pointStorage,pointData.data(),sizeof(pointData));
    std::array<SpotGPU,max_lights> spotData{};
    std::array<Vertex,cone_vertices*max_lights> coneData{};
    for(unsigned i=0;i<s.spotCount;++i) {
        auto& source=s.spots[i];auto& target=spotData[i];
        std::copy(source.position,source.position+3,target.position);
        Vec3 direction=lightDirection(source);
        target.directionInner[0]=direction.x;target.directionInner[1]=direction.y;
        target.directionInner[2]=direction.z;
        target.directionInner[3]=std::cos(source.innerAngle*pi/180.f);
        std::copy(source.color,source.color+3,target.colorIntensity);
        target.colorIntensity[3]=source.intensity;
        target.outerCos[0]=std::cos(source.outerAngle*pi/180.f);
        auto cone=makeCone(source);
        std::copy(cone.begin(),cone.end(),coneData.begin()+i*cone_vertices);
    }
    upload(s.spotStorage,spotData.data(),sizeof(spotData));
    if(s.showMarkers)upload(s.cones,coneData.data(),sizeof(coneData));

    for(unsigned i=0;i<s.objectCount;++i) {
        auto& object=s.objects[i];
        float phase=static_cast<float>(s.animationTime)+i*1.7f;
        Vec3 position{object.position[0]+s.orbitRadius*(std::cos(phase)-std::cos(i*1.7f)),
                      object.position[1],object.position[2]+s.orbitRadius*std::sin(phase)};
        if(!s.animate)position={object.position[0],object.position[1],object.position[2]};
        Mat4 rotation=mul(rotZ(object.rotation[2]*pi/180.f),
            mul(rotY((object.rotation[1]+(s.animate?static_cast<float>(s.animationTime)*45.f:0.f))*pi/180.f),
                rotX(object.rotation[0]*pi/180.f)));
        Vec3 dimensions{object.size[0],object.size[1],object.size[2]};
        Mat4 model=mul(translate(position),mul(rotation,scale(dimensions)));
        Mat4 normalMatrix=mul(rotation,scale({1.f/dimensions.x,1.f/dimensions.y,1.f/dimensions.z}));
        setModel(i,model,normalMatrix,object.albedo,object.specular,object.shininess);
    }
    const float noSpecular[3]{};
    for(unsigned i=0;i<s.pointCount;++i) {
        auto& light=s.points[i];
        setModel(max_objects+i,translate({light.position[0],light.position[1],light.position[2]}),
            identity(),light.color,noSpecular,1);
    }
    for(unsigned i=0;i<s.spotCount;++i) {
        auto& light=s.spots[i];
        setModel(max_objects+max_lights+i,
            translate({light.position[0],light.position[1],light.position[2]}),
            identity(),light.color,noSpecular,1);
    }

    vkResetCommandBuffer(cmd,0);
    VkCommandBufferBeginInfo begin{.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    vkBeginCommandBuffer(cmd,&begin);
    VkClearValue clears[2]{};
    clears[0].color={{.035f,.045f,.07f,1}};
    clears[1].depthStencil={1,0};
    VkRenderPassBeginInfo pass{.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass=context.render_pass,.framebuffer=fd.framebuffer,
        .renderArea={{0,0},context.swapchain_extent},.clearValueCount=2,.pClearValues=clears};
    vkCmdBeginRenderPass(cmd,&pass,VK_SUBPASS_CONTENTS_INLINE);
    VkViewport viewport{0,0,static_cast<float>(context.swapchain_extent.width),
        static_cast<float>(context.swapchain_extent.height),0,1};
    VkRect2D scissor{{0,0},context.swapchain_extent};
    vkCmdSetViewport(cmd,0,1,&viewport);vkCmdSetScissor(cmd,0,1,&scissor);
    VkDeviceSize offset=0;
    vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,s.litPipeline);
    vkCmdBindVertexBuffers(cmd,0,1,&s.octahedron.handle,&offset);
    for(unsigned i=0;i<s.objectCount;++i) {
        VkDescriptorSet sets[2]{s.sceneDescriptor,s.models[i].descriptor};
        vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,s.pipelineLayout,0,2,sets,0,nullptr);
        vkCmdDraw(cmd,24,1,0,0);
    }
    if(s.showMarkers) {
        vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,s.markerPipeline);
        vkCmdBindVertexBuffers(cmd,0,1,&s.cube.handle,&offset);
        for(unsigned i=0;i<s.pointCount;++i) {
            VkDescriptorSet sets[2]{s.sceneDescriptor,s.models[max_objects+i].descriptor};
            vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,s.pipelineLayout,0,2,sets,0,nullptr);
            vkCmdDraw(cmd,36,1,0,0);
        }
        for(unsigned i=0;i<s.spotCount;++i) {
            VkDescriptorSet sets[2]{s.sceneDescriptor,s.models[max_objects+max_lights+i].descriptor};
            vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,s.pipelineLayout,0,2,sets,0,nullptr);
            vkCmdDraw(cmd,36,1,0,0);
        }
        vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,s.linePipeline);
        vkCmdBindVertexBuffers(cmd,0,1,&s.cones.handle,&offset);
        for(unsigned i=0;i<s.spotCount;++i) {
            VkDescriptorSet sets[2]{s.sceneDescriptor,s.models[max_objects+max_lights+i].descriptor};
            vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,s.pipelineLayout,0,2,sets,0,nullptr);
            vkCmdDraw(cmd,cone_vertices,1,i*cone_vertices,0);
        }
    }
    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);
}

} // namespace application

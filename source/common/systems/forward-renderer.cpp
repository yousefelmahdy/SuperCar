#include "forward-renderer.hpp"
#include "../mesh/mesh-utils.hpp"
#include "../texture/texture-utils.hpp"
#define DIRECTIONAL 0
#define POINT 1
#define SPOT 2
namespace our {

    void ForwardRenderer::initialize(glm::ivec2 windowSize, const nlohmann::json& config){
        // First, we store the window size for later use
        this->windowSize = windowSize;

        // Then we check if there is a sky texture in the configuration
        if(config.contains("sky")){
            // First, we create a sphere which will be used to draw the sky
            this->skySphere = mesh_utils::sphere(glm::ivec2(16, 16));
            
            // We can draw the sky using the same shader used to draw textured objects
            ShaderProgram* skyShader = new ShaderProgram();
            skyShader->attach("assets/shaders/textured.vert", GL_VERTEX_SHADER);
            skyShader->attach("assets/shaders/textured.frag", GL_FRAGMENT_SHADER);
            skyShader->link();
            
            //TODO: (Req 9) Pick the correct pipeline state to draw the sky
            // Hints: the sky will be draw after the opaque objects so we would need depth testing but which depth funtion should we pick?
            // We will draw the sphere from the inside, so what options should we pick for the face culling.
            
            PipelineState skyPipelineState;
            //first we enable faceculling
            skyPipelineState.faceCulling.enabled = true;
            //then we tell opengl that the removed face is the front face of object
            skyPipelineState.faceCulling.culledFace = GL_FRONT;

            //first we enable the depth testing to apply the depth test equation in the following line
            skyPipelineState.depthTesting.enabled = true;
            skyPipelineState.depthTesting.function = GL_LEQUAL;

            // Load the sky texture (note that we don't need mipmaps since we want to avoid any unnecessary blurring while rendering the sky)
            std::string skyTextureFile = config.value<std::string>("sky", "");
            Texture2D* skyTexture = texture_utils::loadImage(skyTextureFile, false);

            // Setup a sampler for the sky 
            Sampler* skySampler = new Sampler();
            skySampler->set(GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            skySampler->set(GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            skySampler->set(GL_TEXTURE_WRAP_S, GL_REPEAT);
            skySampler->set(GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

            // Combine all the aforementioned objects (except the mesh) into a material 
            this->skyMaterial = new TexturedMaterial();
            this->skyMaterial->shader = skyShader;
            this->skyMaterial->texture = skyTexture;
            this->skyMaterial->sampler = skySampler;
            this->skyMaterial->pipelineState = skyPipelineState;
            this->skyMaterial->tint = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
            this->skyMaterial->alphaThreshold = 1.0f;
            this->skyMaterial->transparent = false;
        }

        // Then we check if there is a postprocessing shader in the configuration
        if(config.contains("postprocess")){
            //TODO: (Req 10) Create a framebuffer
            //if there is a postprocess framebuffer then generate it and bind it
            glGenFramebuffers(1, &postprocessFrameBuffer);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, postprocessFrameBuffer);
            
            //TODO: (Req 10) Create a color and a depth texture and attach them to the framebuffer
            // Hints: The color format can be (Red, Green, Blue and Alpha components with 8 bits for each channel).
            // The depth format can be (Depth component with 24 bits).
            //using the empty function created in requirement 4 we create the following two textures by specifing the formte of the texture and the window size
            colorTarget = texture_utils::empty(GL_RGBA8,windowSize);
            depthTarget = texture_utils::empty(GL_DEPTH_COMPONENT24, windowSize);
            //attach the framebuffer to the above two textures
            glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTarget->getOpenGLName(), 0);
            glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTarget->getOpenGLName(), 0);
            
            //TODO: (Req 10) Unbind the framebuffer just to be safe
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
            
            // Create a vertex array to use for drawing the texture
            glGenVertexArrays(1, &postProcessVertexArray);

            // Create a sampler to use for sampling the scene texture in the post processing shader
            Sampler* postprocessSampler = new Sampler();
            postprocessSampler->set(GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            postprocessSampler->set(GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            postprocessSampler->set(GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            postprocessSampler->set(GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

            // Create the post processing shader
            ShaderProgram* postprocessShader = new ShaderProgram();
            postprocessShader->attach("assets/shaders/fullscreen.vert", GL_VERTEX_SHADER);
            postprocessShader->attach(config.value<std::string>("postprocess", ""), GL_FRAGMENT_SHADER);
            postprocessShader->link();

            // Create a post processing material
            postprocessMaterial = new TexturedMaterial();
            postprocessMaterial->shader = postprocessShader;
            postprocessMaterial->texture = colorTarget;
            postprocessMaterial->sampler = postprocessSampler;
            // The default options are fine but we don't need to interact with the depth buffer
            // so it is more performant to disable the depth mask
            postprocessMaterial->pipelineState.depthMask = false;
        }
    }

    void ForwardRenderer::destroy(){
        // Delete all objects related to the sky
        if(skyMaterial){
            delete skySphere;
            delete skyMaterial->shader;
            delete skyMaterial->texture;
            delete skyMaterial->sampler;
            delete skyMaterial;
        }
        // Delete all objects related to post processing
        if(postprocessMaterial){
            glDeleteFramebuffers(1, &postprocessFrameBuffer);
            glDeleteVertexArrays(1, &postProcessVertexArray);
            delete colorTarget;
            delete depthTarget;
            delete postprocessMaterial->sampler;
            delete postprocessMaterial->shader;
            delete postprocessMaterial;
        }
    }

    void ForwardRenderer::render(World* world){
        // First of all, we search for a camera and for all the mesh renderers
        CameraComponent* camera = nullptr;
        opaqueCommands.clear();
        transparentCommands.clear();
        lightedCommands.clear();
        for(auto entity : world->getEntities()){
            // If we hadn't found a camera yet, we look for a camera in this entity
            if(!camera) camera = entity->getComponent<CameraComponent>();
            // If this entity has a mesh renderer component
            if(auto meshRenderer = entity->getComponent<MeshRendererComponent>(); meshRenderer){
                // We construct a command from it
                RenderCommand command;
                command.localToWorld = meshRenderer->getOwner()->getLocalToWorldMatrix();
                command.center = glm::vec3(command.localToWorld * glm::vec4(0, 0, 0, 1));
                command.mesh = meshRenderer->mesh;
                command.material = meshRenderer->material;
                // if it is transparent, we add it to the transparent commands list
                if(command.material->transparent){
                    transparentCommands.push_back(command);
                } else if (command.material->lighted){
                // if it is lighted, we add it to the transparent commands list
                    lightedCommands.push_back(command);
                } else {
                // Otherwise, we add it to the opaque command list
                    opaqueCommands.push_back(command);
                }
            }
        }

        // If there is no camera, we return (we cannot render without a camera)
        if(camera == nullptr) return;

        //TODO: (Req 8) Modify the following line such that "cameraForward" contains a vector pointing the camera forward direction
        // HINT: See how you wrote the CameraComponent::getViewMatrix, it should help you solve this one
        glm::vec3 cameraForward = camera->getOwner()->getLocalToWorldMatrix() * glm::vec4(0, 0, -1, 1);
        std::sort(transparentCommands.begin(), transparentCommands.end(), [cameraForward](const RenderCommand& first, const RenderCommand& second){
            //TODO: (Req 8) Finish this function
            // HINT: the following return should return true "first" should be drawn before "second". 
            float fz = glm::dot(first.center, cameraForward);
            float sz = glm::dot(second.center, cameraForward);
            return fz<sz;
        });

        //TODO: (Req 8) Get the camera ViewProjection matrix and store it in VP
        glm::mat4 VP = camera->getProjectionMatrix(windowSize) * camera->getViewMatrix();
        //TODO: (Req 8) Set the OpenGL viewport using windowSize
        glViewport(0, 0, windowSize[0], windowSize[1]);
        //TODO: (Req 8) Set the clear color to black and the clear depth to 1
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClearDepth(1.0);
        //TODO: (Req 8) Set the color mask to true and the depth mask to true (to ensure the glClear will affect the framebuffer)
        glDepthMask(true);
        glColorMask(true, true, true, true);

        // If there is a postprocess material, bind the framebuffer
        if(postprocessMaterial){
            //TODO: (Req 10) bind the framebuffer
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, postprocessFrameBuffer);
        }

        //TODO: (Req 8) Clear the color and depth buffers
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        //TODO: (Req 8) Draw all the opaque commands
        // Don't forget to set the "transform" uniform to be equal the model-view-projection matrix for each render command
        for(int i=0; i<opaqueCommands.size(); i++){
            opaqueCommands[i].material->setup();
            ShaderProgram* shader = opaqueCommands[i].material->shader;
            shader->set("transform", VP * opaqueCommands[i].localToWorld);
            opaqueCommands[i].mesh->draw();
        }
        // Draw all the lighted commands
        for (int i=0; i<lightedCommands.size(); i++){
            lightedCommands[i].material->setup();

            glm::mat4 M = lightedCommands[i].localToWorld;
            glm::mat4 M_IT = glm::transpose(glm::inverse(M));
            glm::vec3 eye = camera->getOwner()->localTransform.position;

            lightedCommands[i].material->shader->set("M", M);
            lightedCommands[i].material->shader->set("M_IT", M_IT);
            lightedCommands[i].material->shader->set("VP", VP);
            lightedCommands[i].material->shader->set("eye", eye);

            lightedCommands[i].material->shader->set("sky.top", glm::vec3(0.3f, 0.6f, 1.0f));
            lightedCommands[i].material->shader->set("sky.middle", glm::vec3(0.3f, 0.3f, 0.3f));
            lightedCommands[i].material->shader->set("sky.bottom", glm::vec3( 0.1f, 0.1f, 0.0f));
            
            lightedCommands[i].material->shader->set("light_count", 3);

            lightedCommands[i].material->shader->set("lights[0].type", DIRECTIONAL);
            lightedCommands[i].material->shader->set("lights[0].direction", glm::vec3(1, 0, 0));
            lightedCommands[i].material->shader->set("lights[0].diffuse", glm::vec3(1, 0.2, 0.1));
            lightedCommands[i].material->shader->set("lights[0].specular", glm::vec3(1, 0.2, 0.1));
            
            lightedCommands[i].material->shader->set("lights[1].type", POINT);
            lightedCommands[i].material->shader->set("lights[1].position", glm::vec3(0, 1.5f, 0));
            lightedCommands[i].material->shader->set("lights[1].diffuse", glm::vec3(1, 0.2, 0.1));
            lightedCommands[i].material->shader->set("lights[1].specular", glm::vec3(1, 0.2, 0.1));
            lightedCommands[i].material->shader->set("lights[1].attenuation",glm::vec3( 1, 0, 0));

            lightedCommands[i].material->shader->set("lights[2].type", SPOT);
            lightedCommands[i].material->shader->set("lights[2].position", glm::vec3( 1, 1, 0));
            lightedCommands[i].material->shader->set("lights[2].direction", glm::vec3(-1, 0, 0));
            lightedCommands[i].material->shader->set("lights[2].diffuse", glm::vec3(1, 0.9, 0.7));
            lightedCommands[i].material->shader->set("lights[2].specular", glm::vec3(1, 0.9, 0.7));
            lightedCommands[i].material->shader->set("lights[2].attenuation",glm::vec3(1, 0, 0));
            lightedCommands[i].material->shader->set("lights[2].cone_angles",glm::vec2( glm::radians(10.0f), glm::radians(11.0f)));

            lightedCommands[i].mesh->draw();
        }
        // If there is a sky material, draw the sky
        if(this->skyMaterial){
            //TODO: (Req 9) setup the sky material
            skyMaterial->setup();
            //TODO: (Req 9) Get the camera position
            glm::vec3 cameraPosition = camera->getOwner()->getLocalToWorldMatrix()*glm::vec4(0,0,0,1);
            //TODO: (Req 9) Create a model matrix for the sky such that it always follows the camera (sky sphere center = camera position)
            glm::mat4 identity(1.0f); 
            glm::mat4 M = glm::translate(identity,cameraPosition);
            //TODO: (Req 9) We want the sky to be drawn behind everything (in NDC space, z=1)
            // We can acheive the is by multiplying by an extra matrix after the projection but what values should we put in it?
            //scaled the Z with zero then translate it with 1 so it is in the far Z direction so any thing will be drawn above it
            glm::mat4 alwaysBehindTransform = glm::mat4(
            //  Row1, Row2, Row3, Row4
                1.0f, 0.0f, 0.0f, 0.0f, // Column1
                0.0f, 1.0f, 0.0f, 0.0f, // Column2
                0.0f, 0.0f, 0.0f, 0.0f, // Column3
                0.0f, 0.0f, 1.0f, 1.0f  // Column4
            );
            //TODO: (Req 9) set the "transform" uniform
            glm::mat4 skyTransform = alwaysBehindTransform * VP * M ;
            skyMaterial->shader->set("transform", skyTransform);
            //TODO: (Req 9) draw the sky sphere
            skySphere->draw();
        }
        //TODO: (Req 8) Draw all the transparent commands
        // Don't forget to set the "transform" uniform to be equal the model-view-projection matrix for each render command
         for(int i=0; i<transparentCommands.size(); i++){
            transparentCommands[i].material->setup();
            ShaderProgram* shader = transparentCommands[i].material->shader;
            shader->set("transform", VP * transparentCommands[i].localToWorld);
            transparentCommands[i].mesh->draw();
           
        }

        // If there is a postprocess material, apply postprocessing
        if(postprocessMaterial){
            //TODO: (Req 10) Return to the default framebuffer
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
            glBindVertexArray(postProcessVertexArray);
            //TODO: (Req 10) Setup the postprocess material and draw the fullscreen triangle
            postprocessMaterial->setup();
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }
    }
}
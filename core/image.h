#pragma once

#include "core/primitive.h"

#include <string>
#include <vector>

namespace core {

enum class ImageFit {
    Cover,
    Contain,
    Stretch
};

class ImagePrimitive {
public:
    ImagePrimitive() = default;

    bool initialize();
    void destroy();

    void setSource(const std::string& source);
    void setFlipVertically(bool value);
    void setBounds(float x, float y, float width, float height);
    void setTint(const Color& tint);
    void setCornerRadius(float radius);
    void setOpacity(float opacity);
    void setTransform(const Transform& transform);
    void setTransformMatrix(const TransformMatrix& matrix);
    void setFit(ImageFit fit);
    void setCoverViewport(bool enabled, const Vec2& canvasSize, const Vec2& viewportOffset);

    bool updateTexture();
    bool hasPendingLoad() const;
    bool isAnimating() const;
    void render(int windowWidth, int windowHeight);

    static bool isSourceReady(const std::string& source);
    static bool consumeRemoteImageReady();
    static void releaseCachedTextures();

private:
    struct SharedResources;

    static SharedResources& sharedResources();
    static bool retainSharedResources();
    static void releaseSharedResources();
    static GLuint compileShader(GLenum type, const char* source);
    static GLuint loadTexture(const std::string& source, bool flipVertically, bool* pending, int* width, int* height);

    bool updateGifTexture(const std::string& resolvedPath);
    void releaseOwnedTexture();
    Vec3 transformPoint(float x, float y) const;
    void rebuildVertices(float* vertices) const;

    std::string source_;
    std::string loadedSource_;
    bool flipVertically_ = false;
    bool loadedFlipVertically_ = false;
    bool pendingLoad_ = false;
    Rect bounds_;
    Color tint_ = {1.0f, 1.0f, 1.0f, 1.0f};
    float radius_ = 0.0f;
    float opacity_ = 1.0f;
    Transform transform_;
    TransformMatrix transformMatrix_;
    bool hasTransformMatrix_ = false;
    ImageFit fit_ = ImageFit::Cover;
    bool hasCoverViewport_ = false;
    Vec2 coverViewportSize_;
    Vec2 coverViewportOffset_;
    GLuint texture_ = 0;
    bool ownsTexture_ = false;
    int textureWidth_ = 0;
    int textureHeight_ = 0;

    std::string loadedGifPath_;
    bool loadedGifFlipVertically_ = false;
    std::vector<unsigned char> gifPixels_;
    std::vector<int> gifDelays_;
    int gifFrameCount_ = 0;
    int gifFrameIndex_ = 0;
    double gifNextFrameTime_ = 0.0;

    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint shaderProgram_ = 0;
    GLint windowSizeLocation_ = -1;
    GLint textureLocation_ = -1;
    GLint tintLocation_ = -1;
    GLint rectLocation_ = -1;
    GLint radiusLocation_ = -1;
};

} // namespace core

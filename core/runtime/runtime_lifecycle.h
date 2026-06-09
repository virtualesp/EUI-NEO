#pragma once

namespace core::dsl {

inline bool Runtime::initialize() {
    return true;
}

inline bool Runtime::initialize(core::window::Handle window) {
    installInputCallbacks(window);
    return true;
}

template <typename ComposeFn>
inline void Runtime::compose(const std::string& pageId, float logicalWidth, float logicalHeight, ComposeFn&& composeFn) {
    const std::vector<runtime::ElementSnapshot> previousStructure = elementStructure_;
    const Screen screen{logicalWidth, logicalHeight};
    ui_.begin(pageId);
    ui_.setFocusedId(focusedId_);
    composeFn(ui_, screen);
    ui_.end();
    ui_.layout(screen);
    elementStructure_ = collectElementStructure();
    syncScrollStateBindings();
    for (const std::string& scope : ui_.consumeReleasedStateScopes()) {
        const std::string childPrefix = scope + ".";
        if (focusedId_ == scope || focusedId_.rfind(childPrefix, 0) == 0) {
            focusedId_.clear();
            ui_.setFocusedId(focusedId_);
        }
    }

    if (elementStructure_ != previousStructure) {
        needsRender_ = true;
        fullRedraw_ = true;
    }

    if (logicalWidth_ != logicalWidth || logicalHeight_ != logicalHeight) {
        needsRender_ = true;
        fullRedraw_ = true;
    }
    logicalWidth_ = logicalWidth;
    logicalHeight_ = logicalHeight;
}

inline bool Runtime::update(core::window::Handle window, float deltaSeconds, float pointerScale, float dpiScale, bool inputEnabled) {
    PointerEvent event = readPointerEvent(window, pointerScale);
    const auto inputEvents = consumeInputEvents(window);
    KeyboardEvent keyboardEvent = inputEvents.first;
    ScrollEvent scrollEvent = inputEvents.second;
    if (!inputEnabled) {
        event.x = -1000000.0;
        event.y = -1000000.0;
        event.deltaX = 0.0;
        event.deltaY = 0.0;
        event.down = false;
        event.pressedThisFrame = false;
        event.releasedThisFrame = true;
        event.rightDown = false;
        event.rightPressedThisFrame = false;
        event.rightReleasedThisFrame = false;
        keyboardEvent = {};
        scrollEvent = {};
    }
    animating_ = false;
    needsCompose_ = false;
    wantsHandCursor_ = false;
    markInstancesUnseen();
    markTimersUnseen();
    if (ImagePrimitive::consumeRemoteImageReady()) {
        fullRedraw_ = true;
        needsRender_ = true;
    }

    syncScrollStateBindings();
    if (scrollEvent.active()) {
        updateScroll(scrollEvent, hitTestScrollable(event, dpiScale));
    }

    if (event.pressedThisFrame) {
        setFocusedId(hitTestFocusable(event, dpiScale));
    }

    const std::string capturedId = capturedInteractionId();
    const std::string hoverTargetId = !capturedId.empty() ? capturedId : hitTestInteractive(event, dpiScale);
    updateElementTree(event, deltaSeconds, dpiScale, hoverTargetId);
    updateDependentVisualDirtyRegions(dpiScale);

    if (keyboardEvent.hasInput()) {
        updateTextInput(keyboardEvent);
    }
    releaseUnseenTimers();
    updateImeCursorRect(window, dpiScale);
    applyCursor(window);

    promoteBackdropBlurDirtyRegions(dpiScale);
    releaseUnseenInstances();

    const bool result = needsRender_;
    needsRender_ = false;
    return result;
}

inline bool Runtime::isAnimating() const {
    return animating_;
}

inline bool Runtime::needsCompose() const {
    return needsCompose_;
}

inline void Runtime::markFullRedraw() {
    fullRedraw_ = true;
    needsRender_ = true;
}

inline void Runtime::render(int windowWidth, int windowHeight, float dpiScale, const Color& clearColor) {
    core::render::RenderBackend* renderBackend = core::render::activeRenderBackend();
    if (renderBackend == nullptr) {
        return;
    }

    if (!renderBackend->ensureRenderCache(windowWidth, windowHeight)) {
        renderBackend->clear(clearColor);
        renderDirect(*renderBackend, windowWidth, windowHeight, dpiScale);
        dirtyRects_.clear();
        fullRedraw_ = false;
        return;
    }
    if (renderBackend->renderCacheWasRecreated()) {
        fullRedraw_ = true;
    }

    const std::vector<Rect> dirtyRects = core::dsl::resolveDirtyRects(dirtyRects_, fullRedraw_, windowWidth, windowHeight, dpiScale);
    if (dirtyRects.empty() && !fullRedraw_) {
        renderBackend->blitRenderCache(windowWidth, windowHeight);
        return;
    }

    renderBackend->beginRenderCacheFrame(windowWidth, windowHeight);

    if (fullRedraw_) {
        renderBackend->setScissor(false, {}, windowHeight);
        renderBackend->clear(clearColor);
        renderDirect(*renderBackend, windowWidth, windowHeight, dpiScale);
    } else {
        for (const Rect& dirty : dirtyRects) {
            renderBackend->setScissor(true, dirty, windowHeight);
            renderBackend->clear(clearColor);
            renderDirect(*renderBackend, windowWidth, windowHeight, dpiScale, &dirty);
        }
        renderBackend->setScissor(false, {}, windowHeight);
    }

    renderBackend->endRenderCacheFrame();
    renderBackend->blitRenderCache(windowWidth, windowHeight);
    dirtyRects_.clear();
    fullRedraw_ = false;
}

inline void Runtime::render(int windowWidth, int windowHeight, float dpiScale) {
    core::render::RenderBackend* renderBackend = core::render::activeRenderBackend();
    if (renderBackend == nullptr) {
        return;
    }

    const RenderTransform identity;
    const std::vector<const Element*> roots = orderedElements(ui_.roots());
    for (const Element* root : roots) {
        prepareTextElement(*root, windowWidth, windowHeight, dpiScale, identity);
    }
    for (const Element* root : roots) {
        renderElement(*renderBackend, *root, windowWidth, windowHeight, dpiScale, identity);
    }
}

inline void Runtime::shutdown(bool releaseCachedImageTextures) {
    releaseGraphicsResources(releaseCachedImageTextures);
    rects_.clear();
    polygons_.clear();
    texts_.clear();
    images_.clear();
    interactions_.clear();
    dirtyKeys_.clear();
    layouts_.clear();
    scrollStates_.clear();
    sliderStates_.clear();
    timers_.clear();
    dependentVisualStates_.clear();
    frameTargets_.clear();
    elementStructure_.clear();
    ui_.clearState();
}

inline void Runtime::releaseGraphicsResources(bool releaseCachedImageTextures) {
    for (auto& item : rects_) {
        if (item.second.initialized) {
            item.second.primitive->destroy();
            item.second.initialized = false;
        }
    }
    for (auto& item : polygons_) {
        if (item.second.initialized) {
            item.second.primitive->destroy();
            item.second.initialized = false;
        }
    }
    for (auto& item : texts_) {
        if (item.second.initialized) {
            item.second.primitive->destroy();
            item.second.initialized = false;
        }
    }
    for (auto& item : images_) {
        if (item.second.initialized) {
            item.second.primitive->destroy();
            item.second.initialized = false;
        }
    }
    if (releaseCachedImageTextures) {
        ImagePrimitive::releaseCachedTextures();
    }
    destroyCursors();
    fullRedraw_ = true;
    needsRender_ = true;
}

inline void Runtime::applyCursor(core::window::Handle window) {
    if (!arrowCursor_) {
        arrowCursor_ = core::window::createStandardCursor(core::window::CursorType::Arrow);
    }
    if (!handCursor_) {
        handCursor_ = core::window::createStandardCursor(core::window::CursorType::Hand);
    }

    core::window::CursorHandle target = wantsHandCursor_ && handCursor_ ? handCursor_ : arrowCursor_;
    if (target != currentCursor_) {
        core::window::setCursor(window, target);
        currentCursor_ = target;
    }
}

inline void Runtime::destroyCursors() {
    if (arrowCursor_) {
        core::window::destroyCursor(arrowCursor_);
        arrowCursor_ = nullptr;
    }
    if (handCursor_) {
        core::window::destroyCursor(handCursor_);
        handCursor_ = nullptr;
    }
    currentCursor_ = nullptr;
}

} // namespace core::dsl

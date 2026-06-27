#pragma once

namespace core::dsl {

inline void Runtime::renderDirect(core::render::RenderBackend& renderBackend, int windowWidth, int windowHeight, float dpiScale, const Rect* dirtyRect) {
    const RenderTransform identity;
    const bool hasScissor = dirtyRect != nullptr;
    const Rect scissor = dirtyRect ? *dirtyRect : Rect{};
    const std::vector<const Element*> roots = orderedElements(ui_.roots());
    for (const Element* root : roots) {
        prepareTextElement(*root, windowWidth, windowHeight, dpiScale, identity, dirtyRect, hasScissor, scissor);
    }
    for (const Element* root : roots) {
        renderElement(renderBackend, *root, windowWidth, windowHeight, dpiScale, identity, dirtyRect, hasScissor, scissor);
    }
}

inline void Runtime::prepareTextElement(
    const Element& element,
    int windowWidth,
    int windowHeight,
    float dpiScale,
    const RenderTransform& inheritedTransform,
    const Rect* dirtyRect,
    bool hasScissor,
    const Rect& scissorRect) {
    if (dirtyRect != nullptr || hasScissor) {
        const auto cached = paintBounds_.find(element.id);
        if (cached != paintBounds_.end()) {
            if (!cached->second.hasSubtree) {
                return;
            }
            const Rect subtree = toPixelRect(cached->second.subtree, dpiScale);
            if (dirtyRect != nullptr && !intersects(subtree, *dirtyRect)) {
                return;
            }
            if (hasScissor && !intersects(subtree, scissorRect)) {
                return;
            }
        }
    }

    const RenderTransform renderTransform = resolveRenderTransform(element, dpiScale, inheritedTransform);
    if (renderTransform.opacity <= 0.001f) {
        return;
    }

    Rect effectiveScissor = scissorRect;
    bool effectiveHasScissor = hasScissor;
    if (element.clip) {
        Rect clipFrame = applyRenderTransform(toPixelRect(element.frame, dpiScale), renderTransform);
        if (effectiveHasScissor) {
            if (!intersectRect(effectiveScissor, clipFrame, effectiveScissor)) {
                return;
            }
        } else {
            effectiveScissor = clipFrame;
            effectiveHasScissor = true;
        }
    }

    if (element.kind == ElementKind::Text) {
        runtime::TextInstance& instance = textInstance(element.id);
        Rect frame = toPixelRect(transformRect({instance.frame.value().x,
                                                instance.frame.value().y,
                                                instance.frame.value().width,
                                                instance.frame.value().height},
                                               instance.frame.value(),
                                               instance.transform.value()), dpiScale);
        frame = applyRenderTransform(frame, renderTransform);
        if ((!dirtyRect || intersects(frame, *dirtyRect)) &&
            (!effectiveHasScissor || intersects(frame, effectiveScissor))) {
            prepareText(element, windowWidth, windowHeight, dpiScale, renderTransform);
        }
    }

    const std::vector<const Element*> children = orderedElements(element.children);
    for (const Element* child : children) {
        prepareTextElement(*child, windowWidth, windowHeight, dpiScale, renderTransform, dirtyRect, effectiveHasScissor, effectiveScissor);
    }
}

inline void Runtime::renderElement(
    core::render::RenderBackend& renderBackend,
    const Element& element,
    int windowWidth,
    int windowHeight,
    float dpiScale,
    const RenderTransform& inheritedTransform,
    const Rect* dirtyRect,
    bool hasScissor,
    const Rect& scissorRect) {
    if (dirtyRect != nullptr || hasScissor) {
        const auto cached = paintBounds_.find(element.id);
        if (cached != paintBounds_.end()) {
            if (!cached->second.hasSubtree) {
                return;
            }
            const Rect subtree = toPixelRect(cached->second.subtree, dpiScale);
            if (dirtyRect != nullptr && !intersects(subtree, *dirtyRect)) {
                return;
            }
            if (hasScissor && !intersects(subtree, scissorRect)) {
                return;
            }
        }
    }

    const RenderTransform renderTransform = resolveRenderTransform(element, dpiScale, inheritedTransform);
    if (renderTransform.opacity <= 0.001f) {
        return;
    }
    Rect effectiveScissor = scissorRect;
    bool effectiveHasScissor = hasScissor;
    if (element.clip) {
        Rect clipFrame = applyRenderTransform(toPixelRect(element.frame, dpiScale), renderTransform);
        if (effectiveHasScissor) {
            if (!intersectRect(effectiveScissor, clipFrame, effectiveScissor)) {
                return;
            }
        } else {
            effectiveScissor = clipFrame;
            effectiveHasScissor = true;
        }
    }

    if (element.kind == ElementKind::Rect) {
        Rect visual = toPixelRect(visualRect(rectInstance(element.id).frame.value(),
                                            rectInstance(element.id).shadow.value(),
                                            rectInstance(element.id).blur.value(),
                                            rectInstance(element.id).transform.value()), dpiScale);
        visual = applyRenderTransform(visual, renderTransform);
        if ((!dirtyRect || intersects(visual, *dirtyRect)) &&
            (!effectiveHasScissor || intersects(visual, effectiveScissor))) {
            applyOptionalScissor(renderBackend, effectiveHasScissor, effectiveScissor, windowHeight);
            renderRect(element, windowWidth, windowHeight, dpiScale, renderTransform);
        }
    } else if (element.kind == ElementKind::Polygon) {
        Rect visual = toPixelRect(visualRect(polygonInstance(element.id).frame.value(),
                                            Shadow{},
                                            0.0f,
                                            polygonInstance(element.id).transform.value()), dpiScale);
        visual = applyRenderTransform(visual, renderTransform);
        if ((!dirtyRect || intersects(visual, *dirtyRect)) &&
            (!effectiveHasScissor || intersects(visual, effectiveScissor))) {
            applyOptionalScissor(renderBackend, effectiveHasScissor, effectiveScissor, windowHeight);
            renderPolygon(element, windowWidth, windowHeight, dpiScale, renderTransform);
        }
    } else if (element.kind == ElementKind::Text) {
        runtime::TextInstance& instance = textInstance(element.id);
        Rect frame = toPixelRect(transformRect({instance.frame.value().x,
                                                instance.frame.value().y,
                                                instance.frame.value().width,
                                                instance.frame.value().height},
                                               instance.frame.value(),
                                               instance.transform.value()), dpiScale);
        frame = applyRenderTransform(frame, renderTransform);
        if ((!dirtyRect || intersects(frame, *dirtyRect)) &&
            (!effectiveHasScissor || intersects(frame, effectiveScissor))) {
            applyOptionalScissor(renderBackend, effectiveHasScissor, effectiveScissor, windowHeight);
            renderText(element, windowWidth, windowHeight, dpiScale, renderTransform);
        }
    } else if (element.kind == ElementKind::Image || element.kind == ElementKind::Svg) {
        Rect visual = toPixelRect(imageVisualRect(imageInstance(element.id).frame.value(),
                                                 imageInstance(element.id).transform.value()), dpiScale);
        visual = applyRenderTransform(visual, renderTransform);
        if ((!dirtyRect || intersects(visual, *dirtyRect)) &&
            (!effectiveHasScissor || intersects(visual, effectiveScissor))) {
            applyOptionalScissor(renderBackend, effectiveHasScissor, effectiveScissor, windowHeight);
            renderImage(element, windowWidth, windowHeight, dpiScale, renderTransform);
        }
    }

    const std::vector<const Element*> children = orderedElements(element.children);
    for (const Element* child : children) {
        renderElement(renderBackend, *child, windowWidth, windowHeight, dpiScale, renderTransform, dirtyRect, effectiveHasScissor, effectiveScissor);
    }
}

inline void Runtime::renderRect(
    const Element& element,
    int windowWidth,
    int windowHeight,
    float dpiScale,
    const RenderTransform& renderTransform) {
    runtime::RectInstance& instance = rectInstance(element.id);
    if (!instance.initialized) {
        instance.initialized = instance.primitive->initialize();
        if (!instance.initialized) {
            return;
        }
    }

    const Rect frame = toPixelRect(instance.frame.value(), dpiScale);
    const Color currentColor = instance.color.value();
    Transform transform = scaleTransform(instance.transform.value(), dpiScale);

    instance.primitive->setBounds(frame.x, frame.y, frame.width, frame.height);
    instance.primitive->setColor(currentColor);
    instance.primitive->setGradient(element.gradient);
    instance.primitive->setBorder(scaleBorder(instance.border.value(), dpiScale));
    instance.primitive->setShadow(scaleShadow(instance.shadow.value(), dpiScale));
    instance.primitive->setCornerRadius(toPixels(instance.radius.value(), dpiScale));
    instance.primitive->setBlur(toPixels(instance.blur.value(), dpiScale));
    instance.primitive->setOpacity(instance.opacity.value() * renderTransform.opacity);
    instance.primitive->setTransformMatrix(combinedPrimitiveMatrix(renderTransform, frame, transform));
    ++core::render::currentRenderFrameStats().rectDraws;
    instance.primitive->render(windowWidth, windowHeight);
}

inline void Runtime::renderPolygon(
    const Element& element,
    int windowWidth,
    int windowHeight,
    float dpiScale,
    const RenderTransform& renderTransform) {
    runtime::PolygonInstance& instance = polygonInstance(element.id);
    if (!instance.initialized) {
        instance.initialized = instance.primitive->initialize();
        if (!instance.initialized) {
            return;
        }
    }

    const Rect frame = toPixelRect(instance.frame.value(), dpiScale);
    Transform transform = scaleTransform(instance.transform.value(), dpiScale);

    instance.primitive->setBounds(frame.x, frame.y, frame.width, frame.height);
    instance.primitive->setPoints(scaledPolygonPoints(instance.points, dpiScale));
    instance.primitive->setRadius(toPixels(instance.radius.value(), dpiScale));
    instance.primitive->setColor(instance.color.value());
    instance.primitive->setOpacity(instance.opacity.value() * renderTransform.opacity);
    instance.primitive->setTransformMatrix(combinedPrimitiveMatrix(renderTransform, frame, transform));
    ++core::render::currentRenderFrameStats().polygonDraws;
    instance.primitive->render(windowWidth, windowHeight);
}

inline void Runtime::prepareText(
    const Element& element,
    int,
    int,
    float dpiScale,
    const RenderTransform& renderTransform) {
    runtime::TextInstance& instance = textInstance(element.id);
    if (!instance.initialized) {
        instance.initialized = instance.primitive->initialize();
        if (!instance.initialized) {
            return;
        }
    }

    const Rect frame = toPixelRect(instance.frame.value(), dpiScale);
    const float maxWidth = element.maxWidth > 0.0f ? toPixels(element.maxWidth, dpiScale) : frame.width;
    const float lineHeight = element.lineHeight > 0.0f ? toPixels(element.lineHeight, dpiScale) : 0.0f;
    Color textColor = instance.color.value();
    Transform transform = scaleTransform(instance.transform.value(), dpiScale);
    textColor.a *= instance.opacity.value();

    float x = frame.x;
    if (element.horizontalAlign == HorizontalAlign::Center) {
        x = frame.x + frame.width * 0.5f;
    } else if (element.horizontalAlign == HorizontalAlign::Right) {
        x = frame.x + frame.width;
    }

    float y = frame.y;
    if (element.verticalAlign == VerticalAlign::Center) {
        y = frame.y + frame.height * 0.5f;
    } else if (element.verticalAlign == VerticalAlign::Bottom) {
        y = frame.y + frame.height;
    }
    textColor.a *= renderTransform.opacity;

    instance.primitive->setPosition(x, y);
    instance.primitive->setTransformMatrix(combinedPrimitiveMatrix(renderTransform, frame, transform));
    instance.primitive->setColor(textColor);
    instance.primitive->setText(instance.text);
    instance.primitive->setFontFamily(instance.fontFamily);
    instance.primitive->setFontSize(toPixels(instance.fontSize, dpiScale));
    instance.primitive->setFontWeight(instance.fontWeight);
    instance.primitive->setMaxWidth(maxWidth);
    instance.primitive->setWrap(instance.wrap);
    instance.primitive->setHorizontalAlign(instance.horizontalAlign);
    instance.primitive->setVerticalAlign(instance.verticalAlign);
    instance.primitive->setLineHeight(lineHeight);
    ++core::render::currentRenderFrameStats().textPrepares;
    instance.primitive->prepare();
}

inline void Runtime::renderText(
    const Element& element,
    int windowWidth,
    int windowHeight,
    float dpiScale,
    const RenderTransform& renderTransform) {
    runtime::TextInstance& instance = textInstance(element.id);
    if (!instance.initialized) {
        instance.initialized = instance.primitive->initialize();
        if (!instance.initialized) {
            return;
        }
    }

    const Rect frame = toPixelRect(instance.frame.value(), dpiScale);
    const float maxWidth = element.maxWidth > 0.0f ? toPixels(element.maxWidth, dpiScale) : frame.width;
    const float lineHeight = element.lineHeight > 0.0f ? toPixels(element.lineHeight, dpiScale) : 0.0f;
    Color textColor = instance.color.value();
    Transform transform = scaleTransform(instance.transform.value(), dpiScale);
    textColor.a *= instance.opacity.value();

    float x = frame.x;
    if (element.horizontalAlign == HorizontalAlign::Center) {
        x = frame.x + frame.width * 0.5f;
    } else if (element.horizontalAlign == HorizontalAlign::Right) {
        x = frame.x + frame.width;
    }

    float y = frame.y;
    if (element.verticalAlign == VerticalAlign::Center) {
        y = frame.y + frame.height * 0.5f;
    } else if (element.verticalAlign == VerticalAlign::Bottom) {
        y = frame.y + frame.height;
    }
    textColor.a *= renderTransform.opacity;

    instance.primitive->setPosition(x, y);
    instance.primitive->setTransformMatrix(combinedPrimitiveMatrix(renderTransform, frame, transform));
    instance.primitive->setColor(textColor);
    instance.primitive->setText(instance.text);
    instance.primitive->setFontFamily(instance.fontFamily);
    instance.primitive->setFontSize(toPixels(instance.fontSize, dpiScale));
    instance.primitive->setFontWeight(instance.fontWeight);
    instance.primitive->setMaxWidth(maxWidth);
    instance.primitive->setWrap(instance.wrap);
    instance.primitive->setHorizontalAlign(instance.horizontalAlign);
    instance.primitive->setVerticalAlign(instance.verticalAlign);
    instance.primitive->setLineHeight(lineHeight);
    ++core::render::currentRenderFrameStats().textDraws;
    instance.primitive->render(windowWidth, windowHeight);
}

inline void Runtime::renderImage(
    const Element& element,
    int windowWidth,
    int windowHeight,
    float dpiScale,
    const RenderTransform& renderTransform) {
    runtime::ImageInstance& instance = imageInstance(element.id);
    if (!instance.initialized) {
        instance.initialized = instance.primitive->initialize();
        if (!instance.initialized) {
            return;
        }
    }

    const Rect frame = toPixelRect(instance.frame.value(), dpiScale);
    Transform transform = scaleTransform(instance.transform.value(), dpiScale);

    instance.primitive->setBounds(frame.x, frame.y, frame.width, frame.height);
    instance.primitive->setTint(instance.tint.value());
    instance.primitive->setCornerRadius(toPixels(instance.radius.value(), dpiScale));
    instance.primitive->setOpacity(instance.opacity.value() * renderTransform.opacity);
    instance.primitive->setTransformMatrix(combinedPrimitiveMatrix(renderTransform, frame, transform));
    instance.primitive->setFit(instance.fit);
    instance.primitive->setCoverViewport(instance.hasCoverViewport,
                                         {toPixels(instance.coverViewportSize.x, dpiScale),
                                          toPixels(instance.coverViewportSize.y, dpiScale)},
                                         {toPixels(instance.coverViewportOffset.x, dpiScale),
                                          toPixels(instance.coverViewportOffset.y, dpiScale)});
    ++core::render::currentRenderFrameStats().imageDraws;
    instance.primitive->render(windowWidth, windowHeight);
}

} // namespace core::dsl

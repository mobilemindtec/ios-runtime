#include "InspectorPageAgent.h"
#include "CachedResource.h"
#include "GlobalObjectInspectorController.h"
#include <JavaScriptCore/Completion.h>
#include <JavaScriptCore/Exception.h>
#include <JavaScriptCore/inspector/ContentSearchUtilities.h>
#include <JavaScriptCore/yarr/RegularExpression.h>
#include <map>
#include <vector>

namespace Inspector {
InspectorPageAgent::InspectorPageAgent(JSAgentContext& context)
    : Inspector::InspectorAgentBase("Page"_s)
    , m_frameIdentifier("NativeScriptMainFrameIdentifier")
    , m_frameUrl("http://main.xml")
    , m_globalObject(*JSC::jsCast<NativeScript::GlobalObject*>(&context.inspectedGlobalObject)) {

    this->m_frontendDispatcher = std::make_unique<PageFrontendDispatcher>(context.frontendRouter);
    this->m_backendDispatcher = PageBackendDispatcher::create(context.backendDispatcher, this);
}

void InspectorPageAgent::didCreateFrontendAndBackend(FrontendRouter* frontendRouter, BackendDispatcher* backendDispatcher) {
}

void InspectorPageAgent::willDestroyFrontendAndBackend(DisconnectReason) {
}

void InspectorPageAgent::enable(ErrorString&) {
}

void InspectorPageAgent::disable(ErrorString&) {
}

void InspectorPageAgent::reload(ErrorString&, const bool* const optionalReloadFromOrigin, const bool* const optionalRevalidateAllResources) {
    JSC::JSValue liveSyncCallback = m_globalObject.get(m_globalObject.globalExec(), JSC::Identifier::fromString(&m_globalObject.vm(), "__onLiveSync"));
    JSC::CallData callData;
    JSC::CallType callType = getCallData(m_globalObject.vm(), liveSyncCallback, callData);
    if (callType == JSC::CallType::None) {
        JSC::JSValue error = JSC::createError(m_globalObject.globalExec(), "global.__onLiveSync is not a function.");
        m_globalObject.inspectorController().reportAPIException(m_globalObject.globalExec(), JSC::Exception::create(m_globalObject.globalExec()->vm(), error));
        return;
    }

    WTF::NakedPtr<JSC::Exception> exception;
    JSC::MarkedArgumentBuffer liveSyncArguments;
    call(m_globalObject.globalExec(), liveSyncCallback, callType, callData, JSC::jsUndefined(), liveSyncArguments, exception);
    if (exception) {
        m_globalObject.inspectorController().reportAPIException(m_globalObject.globalExec(), exception);
    }
}

void InspectorPageAgent::navigate(ErrorString&, const String& in_url) {
    ASSERT_NOT_REACHED();
}

void InspectorPageAgent::getCookies(ErrorString&, RefPtr<JSON::ArrayOf<Inspector::Protocol::Page::Cookie>>& cookies) {
    cookies = JSON::ArrayOf<Inspector::Protocol::Page::Cookie>::create();
}

void InspectorPageAgent::deleteCookie(ErrorString&, const String& in_cookieName, const String& in_url) {
    ASSERT_NOT_REACHED();
}

void InspectorPageAgent::getResourceTree(ErrorString&, RefPtr<Inspector::Protocol::Page::FrameResourceTree>& out_frameTree) {

    Ref<Inspector::Protocol::Page::Frame> frameObject = Inspector::Protocol::Page::Frame::create()
                                                            .setId(m_frameIdentifier)
                                                            .setLoaderId("Loader Identifier")
                                                            .setUrl(m_frameUrl)
                                                            .setSecurityOrigin("")
                                                            .setMimeType("text/xml")
                                                            .release();

    WTF::HashMap<WTF::String, Inspector::CachedResource>& resources = Inspector::cachedResources(this->m_globalObject);

    RefPtr<JSON::ArrayOf<Inspector::Protocol::Page::FrameResource>> subresources = JSON::ArrayOf<Inspector::Protocol::Page::FrameResource>::create();
    out_frameTree = Inspector::Protocol::Page::FrameResourceTree::create()
                        .setFrame(frameObject.copyRef())
                        .setResources(subresources.copyRef())
                        .release();

    for (auto resource : resources) {
        CachedResource& cachedResource = resource.value;
        Ref<Inspector::Protocol::Page::FrameResource> frameResource = Inspector::Protocol::Page::FrameResource::create()
                                                                          .setUrl(resource.key)
                                                                          .setType(cachedResource.type())
                                                                          .setMimeType(cachedResource.mimeType())
                                                                          .release();

        subresources->addItem(WTFMove(frameResource));
    }
}

void InspectorPageAgent::searchInResource(ErrorString& out_error, const String& frameId, const String& url, const String& query, const bool* optionalCaseSensitive, const bool* optionalIsRegex, const String* optionalRequestId, RefPtr<JSON::ArrayOf<Inspector::Protocol::GenericTypes::SearchMatch>>& out_result) {

    out_result = JSON::ArrayOf<Inspector::Protocol::GenericTypes::SearchMatch>::create();

    bool isRegex = optionalIsRegex ? *optionalIsRegex : false;
    bool caseSensitive = optionalCaseSensitive ? *optionalCaseSensitive : false;

    WTF::HashMap<WTF::String, Inspector::CachedResource>& resources = Inspector::cachedResources(this->m_globalObject);
    auto iterator = resources.find(url);
    if (iterator != resources.end()) {
        CachedResource& resource = iterator->value;
        ErrorString out_error;
        WTF::String content = resource.content(out_error);
        if (out_error.isEmpty()) {
            out_result = ContentSearchUtilities::searchInTextByLines(content, query, caseSensitive, isRegex);
        }
    }
}

static Ref<Inspector::Protocol::Page::SearchResult> buildObjectForSearchResult(const String& frameId, const String& url, int matchesCount) {
    return Inspector::Protocol::Page::SearchResult::create()
        .setUrl(url)
        .setFrameId(frameId)
        .setMatchesCount(matchesCount)
        .release();
}

void InspectorPageAgent::searchInResources(ErrorString&, const String& in_text, const bool* in_caseSensitive, const bool* in_isRegex, RefPtr<JSON::ArrayOf<Inspector::Protocol::Page::SearchResult>>& out_result) {
    out_result = JSON::ArrayOf<Inspector::Protocol::Page::SearchResult>::create();

    bool isRegex = in_isRegex ? *in_isRegex : false;
    bool caseSensitive = in_caseSensitive ? *in_caseSensitive : false;
    JSC::Yarr::RegularExpression regex = ContentSearchUtilities::createSearchRegex(in_text, caseSensitive, isRegex);

    WTF::HashMap<WTF::String, Inspector::CachedResource>& resources = Inspector::cachedResources(this->m_globalObject);
    for (CachedResource& cachedResource : resources.values()) {
        ErrorString out_error;
        WTF::String out_content = cachedResource.content(out_error);
        if (out_error.isEmpty()) {
            int matchesCount = ContentSearchUtilities::countRegularExpressionMatches(regex, out_content);
            if (matchesCount) {
                out_result->addItem(buildObjectForSearchResult(m_frameIdentifier, cachedResource.displayName(), matchesCount));
            }
        }
    }
}

void InspectorPageAgent::setShowPaintRects(ErrorString&, bool in_result) {
    ASSERT_NOT_REACHED();
}

void InspectorPageAgent::setShowRulers(ErrorString&, bool in_result) {
    ASSERT_NOT_REACHED();
}

void InspectorPageAgent::setEmulatedMedia(ErrorString&, const String& in_media) {
}

void InspectorPageAgent::getCompositingBordersVisible(ErrorString&, bool* out_result) {
}

void InspectorPageAgent::setCompositingBordersVisible(ErrorString&, bool in_visible) {
    ASSERT_NOT_REACHED();
}

void InspectorPageAgent::snapshotNode(ErrorString&, int in_nodeId, String* out_dataURL) {
    ASSERT_NOT_REACHED();
}

void InspectorPageAgent::snapshotRect(ErrorString&, int in_x, int in_y, int in_width, int in_height, const String& in_coordinateSystem, String* out_dataURL) {
    ASSERT_NOT_REACHED();
}

void InspectorPageAgent::archive(ErrorString&, String* out_data) {
    ASSERT_NOT_REACHED();
}

void InspectorPageAgent::getResourceContent(ErrorString& errorString, const String& in_frameId, const String& in_url, String* out_content, bool* out_base64Encoded) {
    if (in_url == m_frameUrl) {
        *out_base64Encoded = false;
        *out_content = WTF::emptyString();

        return;
    }

    WTF::HashMap<WTF::String, Inspector::CachedResource>& resources = Inspector::cachedResources(this->m_globalObject);
    auto iterator = resources.find(in_url);
    if (iterator == resources.end()) {
        errorString = "No such item"_s;

        return;
    }

    CachedResource& resource = iterator->value;

    *out_base64Encoded = !resource.hasTextContent();
    *out_content = resource.content(errorString);
}
}

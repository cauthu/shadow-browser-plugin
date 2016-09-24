#ifndef ResourceClient_hpp
#define ResourceClient_hpp


namespace blink {

class Resource;

class ResourceClient {
public:
    enum ResourceClientType {
        BaseResourceType,
        ImageType,
        FontType,
        StyleSheetType,
        DocumentType,
        RawResourceType
    };

    virtual ~ResourceClient() { }
    virtual void notifyFinished(Resource*) { }

    static ResourceClientType expectedType() { return BaseResourceType; }
    virtual ResourceClientType resourceClientType() const { return expectedType(); }

protected:
    ResourceClient() { }
};
}

#endif

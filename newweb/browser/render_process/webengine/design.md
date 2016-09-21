(accurate as of chrome 38.0.2125.122)


A web resource is a remote object like an image, a script,
etc. typically identified by a URI

multiple elements on the same page can refer to same resource, e.g.,
multiple "<img>" elements with the same "src" attr

an HTML page is an `HTMLDocument`, a subclass of `Document`. a page
might contain iframes, and each iframe is typically another
`Document`. thus a page has a _main_ document and possibly many child
documents.

each `Document` has a `ResourceFetcher`, from which the document
requests Resources, e.g.:

```
sharedptr<Resource> resource = fetcher->fetch( <ResourceRequest> )
resource->add_client/add_observer(this)
```

where `ResourceRequest` contains things like the URI, headers, desired
timeout, whether-to-bypass-the-cache, etc.

(technically the above usually only creates the resource object, and
there's a separate function `Resource::load()` to actually starts
loading the resource)

as the data for the resource comes in, the resource will save the data
in a buffer, and for certain resource types, (e.g., `RawResource` used
for html of main document) will also notify all its clients/observers
via an api such as.

```
virtual void dataReceived(Resource*, const char* /* data */, int /* length */) { }
```

this is because, for example, the html can be parsed
incrementally. instead of having to wait for the whole html file to be
received.

for us, we don't care about the actual data content, only the progress
in number of bytes, etc.

when the resource has finished loading, it will notify all its client.

note that after the resource has finished downloaded, another client
(e.g., a second `<img>` element that refers to the same URI) can
request it and add itself as client/obserer of resource. in this case,
the resource should have a way to let the client know its current
progress and/or whether it is already finished.



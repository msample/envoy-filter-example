Header Injection
================

Header injection is a flexible filter that can add headers to upstream-bound requests based on the original headers
in the requests. The logic that computes the headers to inject is external to Envoy and accessed via a gRPC call.
Header injection is conditional on trigger and antitrigger headers being present. This allows multiple injection
filters to be stacked yet only one of them fires. For example, if the first injection filter is triggered and
successfully injects some headers, subsequent injection filters can use those injected headers as anti-triggers
so they don't fire.  This is important as the gRPC call must complete before Envoy can forward the request to
the upstream cluster.

Stacking injection filters may make sense if you have multiple sources of authentication such as session cookies
and OAUTH tokens.  Each injection filter can talk to a different gRPC injection service, for example one backed by
your OAUTH authorization server while another talks to your session service.

.. code-block:: json

  {
    "type": "decode",
    "name": "inject",
    "config": {
      "antitrigger_headers": [],
      "trigger_headers": [],
      "include_headers": [],
      "upstream_inject_headers": [],
      "upstream_remove_headers": [],
      "cluster_name": "...",
      "timeout_ms": "..."
    }
  }

antitrigger_headers
  *(optional, array)* header name strings where any of which present
  in a request will disable injection.  For example, if antitriggers
  has "x-skip-injecton" and the request has that header with any
  non-empty value, this injection filter will not attempt to inject
  any headers.

trigger_headers
  *(required, array)* header name strings, where any of which present
  in a request will cause injection to be attempted unless an
  antitrigger is present.  These headers names also support
  "cookie.(cookie-name)" syntax so you can trigger on the presence of
  a specific cookie. For example, "cookie.session" will trigger
  injection if a cookie named "session" (case sensitive) is present in
  the request.  All trigger headers will be passed as parameters to
  the gRPC injection request, for example, to allow a session id to be
  converted to a JWT containing the user id.  Named cookie 'headers'
  are passed with the "cookie.(name)" name and a value that is just
  the named cookie value with any optional quotes removed.

include
  *(optional, array)* if triggered, these header names and values will
  be included as parameters to the gRPC injection request along with
  any present trigger headers. They provide information to the
  injection sevice in order to compute the injected header values.
  The follwoing HTTP2 pseudo-headers are available here: :path,
  :authority, :method.  The :path includes query parameters.

upstream_inject_headers
  *(required, array)* header name strings desired to be injected into
  the upstream request.  These will be returned in the gRPC inject
  response.  Only headers named in this list are allowed to be
  injected.  Any others returned in the gRPC response will be ignored.
  Strongly consider also adding these to the *internal_only_headers*
  of the *route_config* so they are stripped first if they arrive from
  outside (no forgeries).  Also consider signatures on these header
  values to prevent forgeries from inside your network. For example,
  use the RSA or ECC signatures on a JWT.  If the injected header
  already exists in the request, the injected one replaces the
  original one.

upstream_remove_headers
  *(optional, array)* header name strings that should be removed from
  the upstream request once injection has been successfully performed.
  The "cookie.(cookie-name)" syntax is also supported here.  This
  allows sensitve headers such as session ids and access tokens to be
  removed from upstream requests after another header is injected with
  a transient token such as a signed JWT with short validity period.

cluster_name
  *(required, string)* cluster to use for the gRPC calls to the
  injection service. This cluster must exist in the config file at
  startup. Dynamic disovery is not supported yet. Ensure that this
  cluster is configured to support gRPC, ie, the http2 feature and
  ssl_context ALPN h2 are set.

timeout_ms
  *(optional, number)* maximum milliseconds to wait for the gRPC
  injection response before simply passing the request upstream
  without injecting any headers.


Failures
========

If header injection fails due to gRPC timeout etc. the request will be
passed through as-is and no headers injected.  Not all internal endpoints may need authentication
or whatever was being injected. However, consideration is being given to adding *failure_header_name*
and *failure_header_value* configuraiton options as a header name and value to include in the upstream
request if injection failure occurs.

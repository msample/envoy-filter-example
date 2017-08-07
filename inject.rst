Header Injection
================

Header injection is a flexible filter that can add headers to
upstream-bound requests based on the original headers in the
requests. The logic that computes the headers to inject is external to
Envoy and accessed via a gRPC call per request.  Header injection is
conditional on trigger and antitrigger headers being present. This
allows multiple injection filters to be stacked yet only one of them
fires. For example, if the first injection filter is triggered and
successfully injects some headers, subsequent injection filters can
use those injected headers as anti-triggers so they don't fire.  This
is important as the gRPC call must complete before Envoy can forward
the request to the upstream cluster.

Stacking injection filters with conditional triggers may make sense if
you have multiple sources of authentication such as session cookies
and OAUTH tokens.  Each injection filter can talk to a different gRPC
injection service, for example one backed by your OAUTH authorization
server while another talks to your session service.

.. code-block:: json

  {
    "type": "both",
    "name": "inject",
    "config": {
      "antitrigger_headers": [],
      "trigger_headers": [],
      "always_triggered": (boolean)
      "include_headers": [],
      "upstream_inject_headers": [],
      "upstream_inject_any": (boolean),
      "upstream_remove_headers": [],
      "cluster_name": "...",
      "timeout_ms": (int)
    }
  }

antitrigger_headers
  *(optional, array)* header name strings where any of which present
  in a request will disable injection.  For example, if antitriggers
  has "x-skip-injecton" and the request has that header with any
  non-empty value, the injection filter will not attempt to inject
  any headers.

trigger_headers
  *(sometimes required, array)* header name strings, where any of
  which present in a request will cause injection to be attempted
  unless an antitrigger is present.  These headers names also support
  "cookie.(cookie-name)" syntax so you can trigger on the presence of
  a specific cookie. For example, "cookie.session" will trigger
  injection if a cookie named "session" (case sensitive) is present in
  the request.  All trigger headers will be passed as parameters to
  the gRPC injection request, for example, to allow a session id to be
  converted to a JWT containing the user id.  Named cookie 'headers'
  are passed with the "cookie.(name)" name and a value that is just
  the named cookie value with any optional quotes removed. If
  always_triggered is not explicitly specified, this field is
  required.

always_triggered:
  *(optional, boolean)* forces this filter to attempt injection for
  every request if set true. Defaults to false if unspecified.  If
  *always_triggered* is explicitly set to false, this filter will only
  fire if the trigger headers are present. If explictly set false and
  trigger_headers is empty or absent this filter will never fire.  If
  *always_triggered* is explictly set, *trigger_headers* becomes
  optional.

include_headers
  *(optional, array)* if triggered, these header names and values will
  be included as parameters to the gRPC injection request along with
  any present trigger headers. They provide information to the
  injection sevice in order to compute the injected header values.
  The follwoing HTTP2 pseudo-headers are available here: :path,
  :authority, :method.  The :path pseudo-header includes query
  parameters. This does not support the named cookie style
  "cookie.foo"; instead just send the entire "cookie" header.

include_all_headers
   *(optional, boolean)* all request headers and available h2 pseudo
   headers will be sent to the injection service in the gRPC request
   payload if set true. Defaults to false if unspecified. If set true
   the *include_headers* configuration option is ignored.

upstream_inject_headers
  *(optional, array)* header name strings desired to be injected into
  the upstream request.  These names will be provided to the gRPC
  inject request and these headers in the response may be injected.
  Only headers named in this list are allowed to be injected unless
  *upstream_inject_any* is true.  Any others returned in the gRPC
  response will be ignored.  The gRPC responder may choose not provide
  values for every one of these. Strongly consider also adding these
  to the *internal_only_headers* of the *route_config* so they are
  stripped first if they arrive from outside (prevent forgeries).
  Also consider signatures on these header values to prevent forgeries
  from inside your network. For example, use the RSA or ECC signatures
  on a JWT.  If the injected header already exists in the request, the
  injected one replaces the original one.

upstream_inject_any
  *(optional, boolean)* inject every header value returned in the gRPC
   response if true. Otherwise, only those named in
   *upstream_inject_headers* are allowed to be injected.

upstream_remove_headers
  *(optional, array)* header name strings that should be removed from
  the upstream request once injection has been successfully performed.
  The "cookie.(cookie-name)" syntax is also supported here.  This
  allows sensitve headers such as session ids and access tokens to be
  removed from upstream requests after another header is injected with
  a transient token such as a signed JWT with short validity period.

downstream_inject_headers
  *(optional, array)* header name strings desired to be injected into
  the downstream response.  These names will be provided to the gRPC
  inject request and only these headers in the response may be
  injected; others returned in the gRPC response will be ignored (see
  *downstream_inject_any to loosen this). If the injected header
  already exists in the downstream response, the injected one replaces
  the original one.

downstream_inject_any

downstream_remove_headers


cluster_name
  *(required, string)* cluster to use for the gRPC calls to the
  injection service. This cluster must exist in the config file at
  startup. Dynamic disovery is not supported yet. Ensure that this
  cluster is configured to support gRPC, ie, the http2 feature and
  ssl_context ALPN h2 are set.

timeout_ms
  *(optional, number)* maximum milliseconds to wait for the gRPC
  injection response before simply passing the request upstream
  without injecting any headers. Defaults to 120. Minimum value is 1


Failures
========

If header injection fails due to gRPC timeout etc. the request will be
passed through as-is and no headers injected.  Not all internal
endpoints may need authentication or whatever was being injected.

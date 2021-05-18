addEventListener("fetch", event => {
  event.respondWith(handleRequest(event.request))
})

function handleRequest(request) {
  return new Response("Hello world", {
    headers: { "content-type": "text/plain" }
  })
}
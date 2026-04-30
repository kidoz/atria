# atria-todo-api

A small CRUD example that exercises routing, middleware, validation, and JSON.

## Build

```bash
meson setup builddir -Dcatch2:tests=false
meson compile -C builddir
```

## Run

```bash
./builddir/examples/todo_api/atria-todo-api
```

## Endpoints

| Method | Path | Description |
|---|---|---|
| GET | `/health` | Liveness check |
| GET | `/api/v1/items` | List all items |
| POST | `/api/v1/items` | Create an item |
| GET | `/api/v1/items/{id}` | Get a single item |
| PUT | `/api/v1/items/{id}` | Replace an item |
| DELETE | `/api/v1/items/{id}` | Delete an item |

## Try it

```bash
curl -i localhost:8080/health

curl -i -X POST -H 'content-type: application/json' \
  -d '{"name":"buy milk"}' localhost:8080/api/v1/items

curl -i localhost:8080/api/v1/items
```

Validation rejects invalid bodies with `422 Unprocessable Entity`:

```bash
curl -i -X POST -H 'content-type: application/json' \
  -d '{"name":""}' localhost:8080/api/v1/items
```

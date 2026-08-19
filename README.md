# System Control Center C++

> A C++17 inventory, customer, user, checkout, and order-management backend with a browser UI.

## Project snapshot

| Area | Details |
| --- | --- |
| Language | C++17 |
| Build | CMake + MinGW Makefiles |
| Platform | Windows; portable transport helpers for POSIX builds |
| Persistence | SQLite with prepared statements and transactions |
| Authentication | bcrypt-compatible password hashing and server-side sessions |
| Web UI | Inja templates, Bootstrap assets, server-rendered HTML |
| Data format | Form requests today; versioned JSON/XML REST API planned |
| Testing | Unit tests, authentication tests, CRUD tests, checkout and order integration tests |

## What this project demonstrates

- Native C++ HTTP and socket programming.
- Explicit HTTP request parsing and response construction.
- Modular route handling for authentication, dashboard, products, administration, and orders.
- SQLite CRUD workflows with prepared statements and transactional inventory changes.
- Role-based access control for administrators, operators, and read-only viewers.
- Password verification, session cookies, logout, and protected routes.
- Sortable product catalog data and server-rendered templates.
- Multi-line checkout, order history, order details, and inventory-restoring refunds.
- CMake target management and repeatable automated tests.
- Bounded rotating request logs.

## Architecture

```text
                    Browser / HTTP client
                              |
                    http_server.cpp
             dispatch, startup, static fallback
                              |
        +---------------------+---------------------+
        |                     |                     |
   auth_routes          admin_routes          order_routes
   dashboard_routes    product_routes         checkout/refunds
        |                     |                     |
        +---------------------+---------------------+
                              |
                 SQLite database + Inja templates

Shared infrastructure:
http_request | http_response | http_transport | session_store
static_assets | app_logger
```

The current repository intentionally implements its own small loopback HTTP server. Route code is separated from transport code so the application can later adopt an established C++ HTTP library without rewriting the business workflows.

## Technology stack

- **C++17** — application code and platform abstractions.
- **CMake** — reproducible application and test targets.
- **Winsock/POSIX sockets** — current transport implementation.
- **SQLite** — local relational persistence using prepared statements and explicit transactions.
- **Inja** — server-side HTML template rendering.
- **nlohmann/json** — template data and structured JSON values.
- **bcrypt-compatible bundled implementation** — password hashing and verification.
- **Bootstrap** — responsive UI styling and form components.

## Main workflows

### Authentication and authorization

- Login and logout with secure session cookies.
- Protected application pages.
- Administrator-only product, entity, and user management.
- Operator/administrator checkout and refund permissions.
- Viewer mode with explicit read-only messaging.

### Inventory and sales

- Sortable Product Master Catalog.
- Business Entities Registry for customers and companies.
- User and Roles Registry.
- Multi-line sales order creation with stock checks.
- Transactional stock deduction during checkout.
- Order detail and invoiced line-item views.
- Full refunds that restore inventory and mark orders as `REFUNDED`.

## Routes

| Method | Route | Purpose |
| --- | --- | --- |
| `GET` | `/` | Login page |
| `POST` | `/login` | Authenticate and create a session |
| `GET` | `/logout` | End the current session |
| `GET` | `/health` | JSON health check |
| `GET` | `/dashboard` | Role-aware dashboard |
| `GET` | `/products-view` | Sortable product catalog |
| `POST` | `/products/create`, `/products/update`, `/products/delete/{id}` | Product administration |
| `GET` | `/entities-view` | Business entity registry |
| `POST` | `/entities/create`, `/entities/update`, `/entities/delete/{id}` | Entity administration |
| `GET` | `/users-view` | User and role registry |
| `POST` | `/users/create`, `/users/update`, `/users/delete/{id}` | User administration |
| `GET` | `/checkout` | Sales order form |
| `POST` | `/checkout/create` | Transactional order creation |
| `GET` | `/orders-view` | Orders and invoiced line items |
| `GET` | `/orders/{id}` | Order detail and refund workflow |
| `POST` | `/orders/{id}/refund` | Authorized full refund |

## Repository layout

```text
include/                  Public module interfaces
src/http_server.cpp       Server startup and route dispatch
src/*_routes.cpp          Feature-specific route modules
src/http_*.cpp            HTTP parsing, responses, and transport
src/session_store.cpp     In-memory session management
src/app_logger.cpp        Rotating request logging
templates/                Server-rendered HTML pages
static/                   Bootstrap and static assets
data/                     SQLite database runtime data
tests/                    Unit and HTTP integration tests
third_party/              Bundled SQLite, Inja, JSON, and bcrypt sources
```

Third-party licensing and attribution details are documented in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Build and run

From PowerShell:

```powershell
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```

Run the local demo:

```powershell
.\system_control_center.exe
```

Open `http://127.0.0.1:8080`. A different port can be supplied as the first argument:

```powershell
.\system_control_center.exe 8081
```

## Verification

The project currently includes three passing test targets:

- `inventory_tests` — inventory domain behavior.
- `bcrypt_tests` — password hashing and verification.
- `http_integration_tests` — authenticated routes, authorization, CRUD, sorting, checkout, orders, and navigation.

Run all tests with:

```powershell
ctest --test-dir build --output-on-failure
```

## Security and production scope

This is a portfolio and learning implementation, not an internet-facing production service. It currently binds to loopback and uses in-memory sessions. Before deployment, it needs request-size limits, CSRF protection, stronger cookie configuration, persistent session strategy, concurrency tests for stock, duplicate-refund protection, and broader input validation.

## Roadmap

This repository is the stable `custom-http-server` milestone. A future sibling backend can reuse the domain behavior and database while adding:

- An established C++ HTTP library.
- Versioned `/api/v1` REST endpoints for desktop applications.
- JSON responses and secure XML product imports.
- XML schema validation, size limits, duplicate detection, rollback, and import audit history.
- Repository/service boundaries that keep SQL out of HTTP handlers.
- OpenAPI documentation, API integration tests, and CI.

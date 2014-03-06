def setup(app):
    from .domain import GObjectDomain

    app.add_domain(GObjectDomain)

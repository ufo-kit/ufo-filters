from docutils import nodes
from sphinx import addnodes
from sphinx.domains import Domain, ObjType
from sphinx.directives import ObjectDescription, Directive
from sphinx.locale import l_
from sphinx.roles import XRefRole
from sphinx.util.docfields import Field
from sphinx.util.nodes import make_refnode


class GObjectObj(ObjectDescription):

    def add_target_and_index(self, fqn, sig, signode):
        current = self.env.temp_data['current-class']

        if fqn not in self.state.document.ids:
            name = '{0}-{1}'.format(current, fqn.split(':')[0])
            signode['names'].append(name)
            signode['ids'].append(name)
            # FIXME: this fails for some names like "minimum", "maximum", etc.
            # self.state.document.note_explicit_target(signode)

            objects = self.env.domaindata['gobj']['objects']
            objects[name] = (self.env.docname, self.objtype)


class GObjectClass(GObjectObj):

    def handle_signature(self, sig, signode):
        self.env.temp_data['current-class'] = sig
        signode += addnodes.desc_annotation('class', ' class ')
        signode += addnodes.desc_name(sig, sig)
        return sig


class GObjectProperty(GObjectObj):

    def handle_signature(self, sig, signode):
        ptype = None
        split = sig.split(':')
        name = split[0]

        if len(split) > 1:
            ptype = split[1]

        quoted = '"{0}"'.format(name)
        signode += addnodes.desc_name(quoted, quoted)

        if ptype:
            signode += nodes.inline(':', ': ')
            signode += addnodes.desc_type(ptype, ptype)

        return sig


class GObjectXRefRole(XRefRole):
    def process_link(self, env, refnode, has_explicit_title, title, target):
        title = has_explicit_title if has_explicit_title else title

        if 'current-class' in env.temp_data:
            current = env.temp_data['current-class']
            target = '{0}-{1}'.format(current, target)

        return title, target


class GObjectDomain(Domain):

    label = 'GObject'
    name = 'gobj'

    object_types = {
        'class': ObjType(l_('class'), 'class'),
        'prop': ObjType(l_('prop'), 'prop'),
    }

    directives = {
        'class':    GObjectClass,
        'prop':     GObjectProperty,
    }

    initial_data = {
        # 'modules': {},
        'objects': {},
    }

    roles = {
        'class': GObjectXRefRole(),
        'prop': GObjectXRefRole(),
    }

    def get_objects(self):
        for fqn, (docname, objtype) in self.data['objects'].items():
            yield (fqn, fqn, objtype, docname, fqn, 1)

    def resolve_xref(self, env, fromdocname, builder, type, target, node, contnode):
        if target[0] == '~':
            target = target[1:]

        doc, _ = self.data['objects'].get(target, (None, None))

        if doc:
            return make_refnode(builder, fromdocname, doc, target, contnode, target)

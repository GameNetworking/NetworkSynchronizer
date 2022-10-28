def can_build(env, platform):
    return True


def configure(env):
    pass


def get_doc_path():
    return "doc_classes"


def get_doc_classes():
    return [
        "SceneSynchronizer",
        "NetworkedController",
        "DataBuffer",
    ]


def is_enabled():
    return True

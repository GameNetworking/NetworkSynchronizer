def can_build(env, platform):
    return True


def configure(env):
    pass


def get_doc_path():
    return "doc_classes"


def get_doc_classes():
    return [
        "DataBuffer",
        "InputNetworkEncoder",
        "NetworkedController",
        "SceneDiff",
        "SceneSynchronizer",
    ]


def is_enabled():
    return True

#ifndef MEXCE_DEBUG_UTILS_INCLUDED
#define MEXCE_DEBUG_UTILS_INCLUDED

#ifndef MEXCE_DEBUG_UTILS_INTERNAL
#error "mexce_debug_utils.h must be included from mexce.h with MEXCE_ENABLE_DEBUG_UTILS defined."
#endif

// This header is intended to be included from within the mexce::impl namespace
// in mexce.h when MEXCE_ENABLE_DEBUG_UTILS is defined.

namespace test_hooks {
    inline bool& allocation_failure_flag() {
        static bool flag = false;
        return flag;
    }

    inline bool& mprotect_failure_flag() {
        static bool flag = false;
        return flag;
    }

    inline bool force_allocation_failure() {
        return allocation_failure_flag();
    }

    inline bool force_mprotect_failure() {
        return mprotect_failure_flag();
    }

    inline void set_force_allocation_failure(bool value) {
        allocation_failure_flag() = value;
    }

    inline void set_force_mprotect_failure(bool value) {
        mprotect_failure_flag() = value;
    }
}

inline string function_name_to_infix_operator(const string& fn)
{
    static map<string, string> op_map = {
        { "add", "+" },
        { "sub", "-" },
        { "mul", "*" },
        { "div", "/" },
        { "pow", "^" },
        { "less_than", "<" }
    };
    auto it = op_map.find(fn);
    if (it == op_map.end()) {
        return "";
    }
    return it->second;
}

inline string function_name_to_unary_operator(const string& fn)
{
    if (fn == "neg") {
        return "-";
    }
    return "";
}

inline string double_to_pretty_string(double v)
{
    stringstream ss;
    ss << v;
    return ss.str();
}

inline string elist_to_string(const elist_t& elist)
{
    deque<std::tuple<string, size_t, vector<string>>> st;

    // root element appears as an unnamed function of 1 argument
    st.push_back(make_tuple(string(), 2, vector<string>{""}));

    for (auto it = elist.rbegin(); it != elist.rend(); ++it) {
        switch (it->type) {
            case Element_type::CFUNC: {
                auto f = it->f;
                st.push_back(make_tuple(string(), f->args.size() + 1, vector<string>{f->name}));
                break;
            }
            case Element_type::CCONST: {
                auto c = it->c;
                get<2>(st.back()).push_back(double_to_pretty_string(c->value));
                break;
            }
            case Element_type::CVAR: {
                auto v = it->v;
                get<2>(st.back()).push_back(v->name);
                break;
            }
        }

        while (get<2>(st.back()).size() == get<1>(st.back())) {
            string& lrs = get<0>(st.back());
            vector<string>& lrv = get<2>(st.back());

            string symbolic_op;
            if ((get<1>(st.back()) == 2) && !(symbolic_op = function_name_to_unary_operator(lrv[0])).empty()) {
                lrs = string("(") + symbolic_op + lrv.back() + ")";
            }
            else if ((get<1>(st.back()) == 3) && !(symbolic_op = function_name_to_infix_operator(lrv[0])).empty()) {
                lrs = string("(") + lrv.back() + symbolic_op;
                lrv.pop_back();
                lrs += lrv.back() + ")";
            }
            else {
                lrs += lrv[0] + "(";
                if (lrv.size() != 1) {
                    while (true) {
                        lrs += lrv.back();
                        lrv.pop_back();
                        if (lrv.size() == 1) {
                            break;
                        }
                        lrs += ", ";
                    }
                }
                lrs += ")";
            }

            if (st.size() != 1) {
                string tmp = lrs;
                st.pop_back();
                get<2>(st.back()).push_back(tmp);
            }
        }
    }

    assert(st.size() == 1);
    string ret = get<0>(st.back());
    return ret.substr(1, ret.size() - 2);
}

#endif // MEXCE_DEBUG_UTILS_INCLUDED

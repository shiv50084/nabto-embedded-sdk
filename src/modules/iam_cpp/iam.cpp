#pragma once

#include "iam.hpp"

namespace nabto {
namespace iam {

std::unique_ptr<Attribute> Attributes::get(const std::string& key)
{
    auto it = attributes_.find(key);
    if (it == attributes_.end()) {
        return nullptr;
    }
    return std::make_unique<Attribute>(attributes_.get(it));
}

void Attributes::merge(const Attributes& attributes)
{
    for (auto a : attributes.attributes_) {
        attributes_[a.first] = a.second;
    }
}

Statement::matches()
{

}

Effect Policy::eval(const std::string& action, const Attributes& attributes)
{
    Effect decistion = Effect::NO_MATCH;
    for (auto stmt : statements_) {
        Effect effect = stmt.eval(action, attributes);
        if (effect != Effect::NO_MATCH) {
            decision = effect;
        }
    }
    return decision;
}

bool Statement::matchActions(const std::string& action)
{
    for (auto a : actions_) {
        if (action == a) {
            return true;
        }
    }
    return false;
}

bool Statement::matchCondition(const Condition& condition, const Attributes& attributes)
{
    return condition.matches(attributes);
}

bool Statement::matchConditions(const Attributes& attributes)
{
    for (auto condition : conditions_) {
        // All conditions has to match else it is a no match.
        if (!condition.matches(attributes)) {
            return false;
        }
    }
}

Effect Statement::eval(const std::string& action, const Attributes& attributes)
{
    if (!matchActions(action)) {
        return Effect::NO_MATCH;
    }

    if (!matchConditions(attributes)) {
        return Effect::NO_MATCH;
    }

    // action and conditions matches
    return effect_;
}

bool IAM::checkIamAccess(const Subject& subject, const std::string& action, const Attributes& objectAttributes, const Attributes& environmentAttributes)
{

}

bool IamPdp::checkAccess(const Subject& subject, const std::string& action, const Attributes& attributes)
{

    Attributes attrs;
    attrs.merge(attributes);
    attrs.merge(subject.getAttributes());

    nabto::iam::Effect result = nabto::iam::Effect::NO_MATCH;

    for (auto policy : subject.policies()) {
        nabto::iam::Effect effect = policy.eval(action, attrs);
        if (effect == Effect::DENY) {
            return false;
        }
        if (effect == Effect::ALLOW) {
            result = allow;
        }
    }
    if (result == Effect::ALLOW) {
        return true;
    }
    return false;
}

} } // namespace

// Generated source file do not edit.
const char* policy_modify_own_user = "{\"version\": 1,\"name\": \"ModifyOwnUser\",\"statements\": [{\"effect\": \"Allow\",\"actions\": [ \"iam:AddFingerprint\", \"iam:RemoveFingerprint\", \"iam:SetName\" ],\"conditions\": { \"StringEqual\": [ { \"Attribute\": \"connection:UserId\" }, { \"Attribute\": \"iam:UserId\" } ]}]}"
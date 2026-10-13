import { useMutation, useQueryClient } from '@tanstack/react-query';
import { useState, type FormEvent } from 'react';
import { useNavigate } from 'react-router';
import { LIMITS, type GroupDetailDto, type GroupJoinPolicy } from '@akeno/shared';
import { Button } from '../../components/Button';
import { SelectField, TextArea, TextField } from '../../components/Field';
import { Modal } from '../../components/Modal';
import { InlineError } from '../../components/States';
import { useToast } from '../../components/Toast';
import { ApiError, api, errorMessage } from '../../lib/api';
import { queryKeys } from '../../lib/query-keys';

/** Create a new group, or edit an existing one when `group` is provided. */
export function GroupFormDialog({
  open,
  onClose,
  group,
  canChangePolicy = true,
}: {
  open: boolean;
  onClose: () => void;
  group?: GroupDetailDto;
  canChangePolicy?: boolean;
}) {
  const queryClient = useQueryClient();
  const navigate = useNavigate();
  const toast = useToast();
  const [form, setForm] = useState({
    name: group?.name ?? '',
    description: group?.description ?? '',
    joinPolicy: (group?.joinPolicy ?? 'invite_only') as GroupJoinPolicy,
  });
  const [errors, setErrors] = useState<Record<string, string>>({});

  const mutation = useMutation({
    mutationFn: () =>
      group
        ? api.patch<GroupDetailDto>(`/groups/${group.id}`, canChangePolicy ? form : { name: form.name, description: form.description })
        : api.post<GroupDetailDto>('/groups', form),
    onSuccess: (result) => {
      void queryClient.invalidateQueries({ queryKey: queryKeys.groups });
      queryClient.setQueryData(queryKeys.group(result.id), result);
      toast.success(group ? 'Group updated.' : 'Group created.');
      onClose();
      if (!group) void navigate(`/groups/${result.id}`);
    },
    onError: (error) => {
      if (error instanceof ApiError) setErrors(error.fields);
    },
  });

  const onSubmit = (event: FormEvent) => {
    event.preventDefault();
    if (form.name.trim().length < LIMITS.groupNameMin) {
      setErrors({ name: `Use at least ${LIMITS.groupNameMin} characters.` });
      return;
    }
    mutation.mutate();
  };

  return (
    <Modal
      open={open}
      onClose={onClose}
      title={group ? 'Edit group' : 'Create a group'}
      description={group ? undefined : 'Groups are persistent communities with their own chat.'}
      footer={
        <>
          <Button variant="ghost" onClick={onClose}>
            Cancel
          </Button>
          <Button variant="primary" type="submit" form="group-form" loading={mutation.isPending}>
            {group ? 'Save changes' : 'Create group'}
          </Button>
        </>
      }
    >
      <form id="group-form" onSubmit={onSubmit} className="flex flex-col gap-4" noValidate>
        <TextField
          label="Group name"
          name="name"
          value={form.name}
          maxLength={LIMITS.groupNameMax}
          onChange={(event) => setForm((current) => ({ ...current, name: event.target.value }))}
          error={errors.name}
          required
          autoFocus
        />
        <TextArea
          label="Description"
          name="description"
          value={form.description}
          maxLength={LIMITS.groupDescriptionMax}
          onChange={(event) => setForm((current) => ({ ...current, description: event.target.value }))}
          error={errors.description}
          placeholder="What is this group about?"
        />
        {canChangePolicy ? (
          <SelectField
            label="Who can join"
            value={form.joinPolicy}
            onChange={(event) => setForm((current) => ({ ...current, joinPolicy: event.target.value as GroupJoinPolicy }))}
            options={[
              { value: 'invite_only', label: 'Invitation only (private)' },
              { value: 'open', label: 'Anyone on the network (listed in Discover)' },
            ]}
          />
        ) : null}
        <InlineError message={mutation.isError ? errorMessage(mutation.error) : null} />
      </form>
    </Modal>
  );
}
